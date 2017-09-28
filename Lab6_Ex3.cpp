/*
===============================================================================
 Name        : main.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : main definition
===============================================================================
*/

#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>

// TODO: insert other include files here
#include "FreeRTOS.h"
#include "task.h"
#include "ITM_write.h"
#include "semphr.h"

#include <mutex>
#include "Fmutex.h"
#include "user_vcom.h"
#include "DigitalIoPin.h"
#include "Calibrate.h"

// TODO: insert other definitions and declarations here


/* the following is required if runtime statistics are to be collected */
extern "C" {

void vConfigureTimerForRunTimeStats( void ) {
	Chip_SCT_Init(LPC_SCTSMALL1);
	LPC_SCTSMALL1->CONFIG = SCT_CONFIG_32BIT_COUNTER;
	LPC_SCTSMALL1->CTRL_U = SCT_CTRL_PRE_L(255) | SCT_CTRL_CLRCTR_L; // set prescaler to 256 (255 + 1), and start timer
}

}
/* end runtime statictics collection */

/* Sets up system hardware */
static void prvSetupHardware(void)
{
	SystemCoreClockUpdate();
	Board_Init();

	/* Initial LED0 state is off */
	Board_LED_Set(0, false);

	// initialize RIT (= enable clocking etc.)
	Chip_RIT_Init(LPC_RITIMER);
	// set the priority level of the interrupt
	// The level must be equal or lower than the maximum priority specified in FreeRTOS config
	// Note that in a Cortex-M3 a higher number indicates lower interrupt priority
	NVIC_SetPriority( RITIMER_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1 );

}

/*Objects needed in ISR*/
volatile uint32_t RIT_count;
SemaphoreHandle_t sbRIT = xSemaphoreCreateBinary();
SemaphoreHandle_t isrDone = xSemaphoreCreateBinary();

DigitalIoPin *steps;
DigitalIoPin *limitSw1;
DigitalIoPin *limitSw2;
DigitalIoPin *dir;
volatile bool step = false;
volatile bool limitSwToggled = true;
volatile uint32_t counter = 0;


extern "C" {
void RIT_IRQHandler(void)
{
	// This used to check if a context switch is required
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;

	// Tell timer that we have processed the interrupt.
	// Timer then removes the IRQ until next match occurs
	Chip_RIT_ClearIntStatus(LPC_RITIMER); // clear IRQ flag

	if(limitSw1->read() && dir->read() == true){
		dir->write(false);
		limitSwToggled = true;
		RIT_count = 0;
	}
	else if(limitSw2->read() && dir->read() == false){
		dir->write(true);
		limitSwToggled = true;
		RIT_count = 0;
	}

	if(RIT_count > 0) {
		RIT_count--;

		/*move motor here, alternate between true and false between every IRQ*/
		step = !step;
		steps->write(step);
	}
	else {
		Chip_RIT_Disable(LPC_RITIMER); // disable timer
		// Give semaphore and set context switch flag if a higher priority task was woken up
		xSemaphoreGiveFromISR(sbRIT, &xHigherPriorityWoken);
	}
	counter++;
	xSemaphoreGiveFromISR(isrDone, &xHigherPriorityWoken);
	// End the ISR and (possibly) do a context switch
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
}

void RIT_start(uint32_t count, int targetSpeed)
{
	int incrementSpeed = 0;
	int initialSpeed = 500;

	// Determine approximate compare value based on clock rate and passed interval
	uint64_t initialValue = (uint64_t) Chip_Clock_GetSystemClockRate() * (uint64_t) initialSpeed / 1000000;
	uint64_t incrementValue;

	RIT_count = count*2;
	bool braking = false;

	limitSwToggled = false;

	// Enable the interrupt signal in NVIC (the interrupt controller)
	NVIC_EnableIRQ(RITIMER_IRQn);

	xSemaphoreGive(isrDone);

	while(xSemaphoreTake(sbRIT,0) != pdTRUE){

		/*Accelerating*/
		if(xSemaphoreTake(isrDone, portMAX_DELAY) == pdTRUE){
			if(counter % 10 == 0 ){
				if((initialSpeed - incrementSpeed) >= targetSpeed){
					incrementValue = (uint64_t) Chip_Clock_GetSystemClockRate() * (uint64_t) incrementSpeed / 1000000;
					// disable timer during configuration
					Chip_RIT_Disable(LPC_RITIMER);

					// enable automatic clear on when compare value==timer value
					// this makes interrupts trigger periodically
					Chip_RIT_EnableCompClear(LPC_RITIMER);
					// reset the counter
					Chip_RIT_SetCounter(LPC_RITIMER, 0);
					Chip_RIT_SetCompareValue(LPC_RITIMER, initialValue-incrementValue);
					// start counting
					Chip_RIT_Enable(LPC_RITIMER);

					if(!braking) incrementSpeed += 2;
				}

				/*Braking*/
				if(RIT_count < 1000){
					if(incrementSpeed >= 4) incrementSpeed -= 4;
					braking = true;
				}
			}

		}
	}

	// Disable the interrupt signal in NVIC (the interrupt controller)
	NVIC_DisableIRQ(RITIMER_IRQn);
	counter = 0;
}

static void controlRIT(void *pvParameters) {
	Calibrate calibrate;
	calibrate.runStepperMotor();
	//x2 because RIT interrupt decrements count after every interrupt and writes true and false in separate RIT ISR:s
	int numOfStepsBetwnLimits = calibrate.getNumOfSteps();
	int targetSpeed = 500;
	/*Try to run faster every time motor touches limit switch or something*/

	TickType_t temp;
	TickType_t temp2;
	while (limitSwToggled) {
		temp = xTaskGetTickCount();
		RIT_start(numOfStepsBetwnLimits, targetSpeed);
		targetSpeed -= 10;
		if(limitSwToggled) temp2 = xTaskGetTickCount() - temp;
	}

	double secs = (double)temp2 / configTICK_RATE_HZ;
	char buffer[64] = {0};
	int pps = 1000000/targetSpeed;
	sprintf(buffer, "PPS: %d \n\r RPM: %d \n\r TargetSpeed: %d \n\r Time: %f seconds", pps, (calibrate.getNumOfSteps()/pps*60)*2, targetSpeed, secs);
	ITM_write(buffer);
}


static void toggleLED(void *pvParameters) {

	while (true) {
		if(limitSw1->read()){
			Board_LED_Set(0, true);
			vTaskDelay(configTICK_RATE_HZ/2);
			Board_LED_Set(0, false);
		}else if(limitSw2->read()){
			Board_LED_Set(1, true);
			vTaskDelay(configTICK_RATE_HZ/2);
			Board_LED_Set(1, false);
		}
	}
}


int main(void) {

	prvSetupHardware();
	ITM_init();

	steps = new DigitalIoPin (0, 24, DigitalIoPin::output, false);
	dir = new DigitalIoPin (1, 0, DigitalIoPin::output, false);
	limitSw1 = new DigitalIoPin (0,27, DigitalIoPin::pullup, true);
	limitSw2 = new DigitalIoPin (0,28, DigitalIoPin::pullup, true);

	xTaskCreate(controlRIT, "Tx",
				configMINIMAL_STACK_SIZE * 5, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(toggleLED, "Rx",
				configMINIMAL_STACK_SIZE * 5, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(cdc_task, "CDC",
				configMINIMAL_STACK_SIZE * 5, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);


	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
