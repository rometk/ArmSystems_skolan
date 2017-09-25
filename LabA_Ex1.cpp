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
#include "semphr.h"
#include "timers.h"
#include "string.h"

#include <mutex>
#include "Fmutex.h"
#include "user_vcom.h"

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

SemaphoreHandle_t binarySemphr = xSemaphoreCreateBinary();

void printingTask (TimerHandle_t xTimer){

	const char helloStr[16] = {"\n\rHello!"};
	const char aarghStr[16] = {"\n\rAargh!!"};

	USB_send((uint8_t*) helloStr, strlen(helloStr));
	if(xSemaphoreTake(binarySemphr, 0) == pdTRUE)
		USB_send((uint8_t*) aarghStr, strlen(aarghStr));
}

void waitingTask (TimerHandle_t xTimer){

	xSemaphoreGive(binarySemphr);
}

int main(void) {

	prvSetupHardware();

	xTaskCreate(cdc_task, "cdc_task",
				configMINIMAL_STACK_SIZE * 3, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	/*Create software timers*/
	TimerHandle_t autoTimer = xTimerCreate("autoTimer", configTICK_RATE_HZ*5, pdTRUE, 0, printingTask);
	TimerHandle_t oneShotTimer = xTimerCreate("oneShotTimer", configTICK_RATE_HZ*20, pdFALSE, 0, waitingTask);

	xTimerStart(autoTimer,0);
	xTimerStart(oneShotTimer,0);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
