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
#include "ITM_write.h"
#include "stdio.h"
#include "string.h"

#include <stdlib.h>
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

struct nmbrAndTimeStamp{
	uint8_t pinNumbr;
	TickType_t timeStamp;
};

nmbrAndTimeStamp pinData;
uint32_t filterTime = 50, tempTickCount = 0;
QueueHandle_t switchQueue = xQueueCreate(10,sizeof(pinData));

extern "C"{
void PIN_INT0_IRQHandler(void){
	// This used to check if a context switch is required
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;

	// Tell timer that we have processed the interrupt.
	// Timer then removes the IRQ until next match occurs
	Chip_PININT_ClearFallStates(LPC_GPIO_PIN_INT, PININTCH0); // clear IRQ flag

	pinData.pinNumbr = 1;
	pinData.timeStamp = xTaskGetTickCountFromISR() - tempTickCount;
	tempTickCount = xTaskGetTickCountFromISR();

	xQueueSendFromISR(switchQueue, &pinData, &xHigherPriorityWoken);

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

	pinData.pinNumbr = 2;
	pinData.timeStamp = xTaskGetTickCountFromISR() - tempTickCount;
	tempTickCount = xTaskGetTickCountFromISR();

	xQueueSendFromISR(switchQueue, &pinData, &xHigherPriorityWoken);

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

	pinData.pinNumbr = 3;
	pinData.timeStamp = xTaskGetTickCountFromISR() - tempTickCount;
	tempTickCount = xTaskGetTickCountFromISR();

	xQueueSendFromISR(switchQueue, &pinData, &xHigherPriorityWoken);

	Board_LED_Toggle(2);
	// End the ISR and (possibly) do a context switch
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
}

bool parseCommand(char* cmdStr){
	for(int i=0; i<64; i++){
		if(cmdStr[i] == ' ' || cmdStr[i] == '\r'){
			cmdStr[i] = 0;
			break;
		}
	}
	const char filterStr[16] = {"filter"};

	if(strcmp(cmdStr, filterStr) != 0)
		return false;

	return true;
}

uint32_t parseNumber(char* cmdStr){
	char countBuffer[16] = {0};

	for(int i=0, j=0; i<64; i++){
		if(cmdStr[i] >= '0' && cmdStr[i] <= '9'){
			countBuffer[j] = cmdStr[i];
			j++;
		}
	}

	uint32_t parsedCount = (uint32_t)strtol(countBuffer,NULL,10);

	return parsedCount;
}


static void pressSwitches(void *pvParameters) {

	const char pinStr[16] = {" ms Button "};
	nmbrAndTimeStamp pin;

	while (true) {
		xQueueReceive(switchQueue, &pin, portMAX_DELAY);
		if(pin.timeStamp >= filterTime){
			char buttonPressInterval[12] = {0};
			sprintf(buttonPressInterval, "%lu", pin.timeStamp);

			char buttonNmbrStr[8] = {0};
			sprintf(buttonNmbrStr, "%d\n\r", pin.pinNumbr);

			USB_send((uint8_t*) buttonPressInterval, strlen(buttonPressInterval));
			USB_send((uint8_t*) pinStr, strlen(pinStr));
			USB_send((uint8_t*) buttonNmbrStr, strlen(buttonNmbrStr));
		}
	}
}

static void confFilter (void *pvParameters){
	vTaskDelay(configTICK_RATE_HZ/10);

	while(true){
		char strBuffer[64] = {0};
		char receiveBuffer[64] = {0};

		uint8_t bufferIterator = 0;
		bool enterPressed = false;

		do{
			uint32_t len = USB_receive((uint8_t*) receiveBuffer, sizeof(receiveBuffer)/sizeof(*receiveBuffer));

			uint8_t receiveIterator = 0;
			while(receiveIterator < len){
				if(receiveBuffer[receiveIterator] != 127){
					if(receiveBuffer[receiveIterator] == '\r') enterPressed = true;
					strBuffer[bufferIterator] = receiveBuffer[receiveIterator];
					bufferIterator++;
					receiveIterator++;
				}else{
					if(bufferIterator > 0){
						strBuffer[bufferIterator] = 0;
						bufferIterator--;
					}
				}
			}

			USB_send((uint8_t*) receiveBuffer, strlen(receiveBuffer));
	}while(!enterPressed);
		USB_send((uint8_t*) "\n\r", 3);

		if(parseCommand(strBuffer)){
			filterTime = parseNumber(strBuffer);
		}
	}
}

int main(void) {

	prvSetupHardware();
	setupPinInterrupts();
	ITM_init();

	xTaskCreate(pressSwitches, "Tx",
				configMINIMAL_STACK_SIZE * 5, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(confFilter, "ConfFilter", configMINIMAL_STACK_SIZE * 5, NULL, (tskIDLE_PRIORITY + 1UL), NULL);

	xTaskCreate(cdc_task, "CDC", configMINIMAL_STACK_SIZE * 3, NULL, (tskIDLE_PRIORITY + 1UL), NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
