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
#include "semphr.h"
#include "ITM_write.h"
#include "stdio.h"

#include <mutex>
#include "Fmutex.h"
#include "user_vcom.h"

#include "DigitalIoPin.h"

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
}

static void setupPinInterrupts(){

	Chip_GPIO_Init(LPC_GPIO);

	/*Select pins for interrupts*/
	Chip_INMUX_PinIntSel(0, 0, 17); //sw1
	Chip_INMUX_PinIntSel(1, 1, 11); //sw2
	Chip_INMUX_PinIntSel(2, 1, 9);  //sw3

	/*Initialize pin interrupts*/
	Chip_PININT_Init(LPC_GPIO_PIN_INT);

	/*Set pin interrupts as edge sensitive*/
	Chip_PININT_SetPinModeEdge(LPC_GPIO_PIN_INT, PININTCH0);
	Chip_PININT_SetPinModeEdge(LPC_GPIO_PIN_INT, PININTCH1);
	Chip_PININT_SetPinModeEdge(LPC_GPIO_PIN_INT, PININTCH2);

	/*Enable falling edge interrupts for selected pins*/
	Chip_PININT_EnableIntLow(LPC_GPIO_PIN_INT, PININTCH0);
	Chip_PININT_EnableIntLow(LPC_GPIO_PIN_INT, PININTCH1);
	Chip_PININT_EnableIntLow(LPC_GPIO_PIN_INT, PININTCH2);

	/*Set priority of each pin IRQ*/
	NVIC_SetPriority(PIN_INT0_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1);
	NVIC_SetPriority(PIN_INT1_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1);
	NVIC_SetPriority(PIN_INT2_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1);

	/*Enable interrupts*/
	NVIC_EnableIRQ(PIN_INT0_IRQn);
	NVIC_EnableIRQ(PIN_INT1_IRQn);
	NVIC_EnableIRQ(PIN_INT2_IRQn);
}

/*Semaphores*/
QueueHandle_t switchQueue = xQueueCreate(10,sizeof(int));
uint32_t numToSend = 0;

extern "C"{
void PIN_INT0_IRQHandler(void){
	// This used to check if a context switch is required
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;

	// Tell timer that we have processed the interrupt.
	// Timer then removes the IRQ until next match occurs
	Chip_PININT_ClearFallStates(LPC_GPIO_PIN_INT, PININTCH0); // clear IRQ flag

	numToSend = 1;
	xQueueSendFromISR(switchQueue, &numToSend, &xHigherPriorityWoken);

	Board_LED_Toggle(0);
	// End the ISR and (possibly) do a context switch
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
}

extern "C"{
void PIN_INT1_IRQHandler(void){
	// This used to check if a context switch is required
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;

	Chip_PININT_ClearFallStates(LPC_GPIO_PIN_INT, PININTCH1);

	numToSend = 2;
	xQueueSendFromISR(switchQueue, &numToSend, &xHigherPriorityWoken);

	Board_LED_Toggle(1);
	// End the ISR and (possibly) do a context switch
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
}

extern "C"{
void PIN_INT2_IRQHandler(void){
	// This used to check if a context switch is required
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;

	Chip_PININT_ClearFallStates(LPC_GPIO_PIN_INT, PININTCH2);

	numToSend = 3;
	xQueueSendFromISR(switchQueue, &numToSend, &xHigherPriorityWoken);

	Board_LED_Toggle(2);
	// End the ISR and (possibly) do a context switch
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
}


static void pressSwitches(void *pvParameters) {
	DigitalIoPin sw1(0, 17, DigitalIoPin::pullup, true);
	DigitalIoPin sw2(1, 11, DigitalIoPin::pullup, true);
	DigitalIoPin sw3(1, 9, DigitalIoPin::pullup, true);

	enum swPressed{
		sw1Pressed,
		sw2Pressed,
		sw3Pressed
	};

	bool swChanged = false;
	int whichSwPressed = sw1Pressed;
	int pvBuffer[8] = {0};

	char sentenceBuffer[64];
	int ones = 0, twos = 0, threes = 0;
	while (1) {
		xQueueReceive(switchQueue, &pvBuffer, portMAX_DELAY);
		if(pvBuffer[0] == 1){
			if(whichSwPressed != sw1Pressed) swChanged = true;
			whichSwPressed = sw1Pressed;
			ones++;
		}else if(pvBuffer[0] == 2){
			if(whichSwPressed != sw2Pressed) swChanged = true;
			whichSwPressed = sw2Pressed;
			twos++;
		}else if(pvBuffer[0] == 3){
			if(whichSwPressed != sw3Pressed) swChanged = true;
			whichSwPressed = sw3Pressed;
			threes++;
		}

		if(swChanged){
			swChanged = false;
			sprintf(sentenceBuffer,"Button 1 pressed %d times\n\rButton 2 pressed %d times\n\rButton 3 pressed %d times\n\r",ones, twos, threes);
			ITM_write(sentenceBuffer);
			ones = 0, twos = 0, threes = 0;
		}

		while(sw1.read() || sw2.read() || sw3.read());
	}
}

int main(void) {

	prvSetupHardware();
	setupPinInterrupts();
	ITM_init();

	xTaskCreate(pressSwitches, "Tx",
				configMINIMAL_STACK_SIZE * 3, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);


	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
