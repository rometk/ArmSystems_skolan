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
#include "queue.h"
#include "timers.h"
#include "ITM_write.h"
#include "stdio.h"
#include "string.h"

#include <stdlib.h>
#include <mutex>
#include "Fmutex.h"
#include "user_vcom.h"

#include "DigitalIoPin.h"
#include "DoorCode.h"

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
	Board_LED_Set(0, true);
}

static void setupPinInterrupts(){

	Chip_GPIO_Init(LPC_GPIO);

	/*Select pins for interrupts*/
	Chip_INMUX_PinIntSel(0, 0, 17); //sw1
	Chip_INMUX_PinIntSel(1, 1, 11); //sw2

	/*Initialize pin interrupts*/
	Chip_PININT_Init(LPC_GPIO_PIN_INT);

	/*Set pin interrupts as edge sensitive*/
	Chip_PININT_SetPinModeEdge(LPC_GPIO_PIN_INT, PININTCH0);
	Chip_PININT_SetPinModeEdge(LPC_GPIO_PIN_INT, PININTCH1);

	/*Enable falling edge interrupts for selected pins*/
	Chip_PININT_EnableIntLow(LPC_GPIO_PIN_INT, PININTCH0);
	Chip_PININT_EnableIntLow(LPC_GPIO_PIN_INT, PININTCH1);

	/*Set priority of each pin IRQ*/
	NVIC_SetPriority(PIN_INT0_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1);
	NVIC_SetPriority(PIN_INT1_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1);

	/*Enable interrupts*/
	NVIC_EnableIRQ(PIN_INT0_IRQn);
	NVIC_EnableIRQ(PIN_INT1_IRQn);
}

uint32_t filterTime = 50, tempTickCount = 0;
QueueHandle_t codeBuffer = xQueueCreate(8,sizeof(int));
bool learningMode = false;

extern "C"{
void PIN_INT0_IRQHandler(void){
	// This used to check if a context switch is required
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;

	// Tell timer that we have processed the interrupt.
	// Timer then removes the IRQ until next match occurs
	Chip_PININT_ClearFallStates(LPC_GPIO_PIN_INT, PININTCH0); // clear IRQ flag

	int taskNmbr = 0;
	if((xTaskGetTickCountFromISR() - tempTickCount) >= filterTime){
		xQueueSendFromISR(codeBuffer, &taskNmbr, &xHigherPriorityWoken);
		tempTickCount = xTaskGetTickCountFromISR();
	}

	// End the ISR and (possibly) do a context switch
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
}

extern "C"{
void PIN_INT1_IRQHandler(void){
	// This used to check if a context switch is required
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;

	Chip_PININT_ClearFallStates(LPC_GPIO_PIN_INT, PININTCH1);

	int taskNmbr = 1;
	if((xTaskGetTickCountFromISR() - tempTickCount) >= filterTime){
		xQueueSendFromISR(codeBuffer, &taskNmbr, &xHigherPriorityWoken);
		tempTickCount = xTaskGetTickCountFromISR();
	}

	// End the ISR and (possibly) do a context switch
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
}

void clearBuffer (TimerHandle_t xTimer){
	int value = 100;
	xQueueSend(codeBuffer, &value, 0);
}

void closeTheDoor (TimerHandle_t xTimer){
	Board_LED_Set(1, false);
	Board_LED_Set(0, true);
}

static void monitorSW3(void *pvParameters) {
	DigitalIoPin sw3(1, 9, DigitalIoPin::pullup, true);

	while (true) {
		TickType_t temp = xTaskGetTickCount();
		while(sw3.read()){
			if((xTaskGetTickCount()-temp) >= configTICK_RATE_HZ*3){
				learningMode = true;
				Board_LED_Set(0, false);
				Board_LED_Set(2, true);
			}
		}

		vTaskDelay(configTICK_RATE_HZ/10);
	}
}

static void validateCode (void *pvParameters){

	DigitalIoPin sw1(0, 17, DigitalIoPin::pullup, true);
	DigitalIoPin sw2(1, 11, DigitalIoPin::pullup, true);

	TimerHandle_t timeout = xTimerCreate("timeout", configTICK_RATE_HZ*5, pdFALSE, 0, clearBuffer);
	xTimerStart(timeout, portMAX_DELAY);
	TimerHandle_t doorOpen = xTimerCreate("timeout", configTICK_RATE_HZ*5, pdFALSE, 0, closeTheDoor);

	DoorCode doorCode;
	int value;
	const int clearBuffer = 100;

	while(true){
		if(!learningMode){
			xQueueReceive(codeBuffer, &value, portMAX_DELAY);
			if(value == clearBuffer) doorCode.resetIterator();
			else{
				xTimerReset(timeout, portMAX_DELAY);

				doorCode.checkCode(value);
				if(doorCode.doorUnlocked()){
					Board_LED_Set(1, true);
					Board_LED_Set(0, false);
					xTimerReset(doorOpen, portMAX_DELAY);
				}
			}
		}else{
			xQueueReceive(codeBuffer, &value, portMAX_DELAY);
			xTimerReset(timeout, portMAX_DELAY);

			doorCode.newDoorCode(value);
			learningMode = !doorCode.newCodeSet();
			Board_LED_Set(2, learningMode);
			Board_LED_Set(0, !learningMode);
		}
	}
}

int main(void) {

	prvSetupHardware();
	setupPinInterrupts();
	ITM_init();

	xTaskCreate(monitorSW3, "monitorSW3",
				configMINIMAL_STACK_SIZE * 5, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(validateCode, "validateCode", configMINIMAL_STACK_SIZE * 5, NULL, (tskIDLE_PRIORITY + 1UL), NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
