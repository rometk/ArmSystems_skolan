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
#include "event_groups.h"
#include "ITM_write.h"
#include <stdlib.h>
#include "string.h"

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

/*FreeRTOS API:s used in this program*/
SemaphoreHandle_t lock = xSemaphoreCreateMutex();
EventGroupHandle_t eventVar = xEventGroupCreate();

#define firstBit (1 << 0)
#define secondBit (1 << 1)
#define thirdBit (1 << 2)
#define fourthBit (1 << 3)

static void pressButton (void *pvParameters) {
	DigitalIoPin sw1(0, 17, DigitalIoPin::pullup, true);

	while(true){

		if(sw1.read()){
			while(sw1.read());
			xEventGroupSetBits(eventVar, firstBit);

			/*Waits for all the tasks to be completed and clears event bit variable*/
			xEventGroupWaitBits(eventVar, secondBit | thirdBit | fourthBit, pdTRUE, pdTRUE, portMAX_DELAY);
		}

		vTaskDelay(configTICK_RATE_HZ/10);
	}
}

static void waitForButton1 (void *pvParameters){
	const char taskStr[32] = "\n\r\n\rTask: 1\n\rTicks elapsed: ";
	int randomSeed = 1;

	while(true){
		xEventGroupWaitBits(eventVar, firstBit, pdFALSE, pdFALSE, portMAX_DELAY);

		srand(randomSeed);
		randomSeed+=2;

		char tickCountStr[16] = {0};

		TickType_t temp = xTaskGetTickCount();
		vTaskDelay((rand() % 1000 + 1000));
		sprintf(tickCountStr, "%lu", xTaskGetTickCount() - temp);

		xSemaphoreTake(lock, portMAX_DELAY);

		USB_send((uint8_t*) taskStr, strlen(taskStr));
		USB_send((uint8_t*) tickCountStr, strlen(tickCountStr));

		xSemaphoreGive(lock);

		xEventGroupClearBits(eventVar, firstBit);
		xEventGroupSetBits(eventVar, secondBit);
	}
}

static void waitForButton2 (void *pvParameters){
	const char taskStr[32] = "\n\r\n\rTask: 2\n\rTicks elapsed: ";
	int randomSeed = 50;

	while(true){
		xEventGroupWaitBits(eventVar, firstBit, pdFALSE, pdFALSE, portMAX_DELAY);

		srand(randomSeed);
		randomSeed+=5;

		char tickCountStr[16] = {0};

		TickType_t temp = xTaskGetTickCount();
		vTaskDelay((rand() % 1000 + 1000));
		sprintf(tickCountStr, "%lu", xTaskGetTickCount() - temp);

		xSemaphoreTake(lock, portMAX_DELAY);

		USB_send((uint8_t*) taskStr, strlen(taskStr));
		USB_send((uint8_t*) tickCountStr, strlen(tickCountStr));

		xSemaphoreGive(lock);

		xEventGroupClearBits(eventVar, firstBit);
		xEventGroupSetBits(eventVar, thirdBit);
	}
}

static void waitForButton3 (void *pvParameters){
	const char taskStr[32] = "\n\r\n\rTask: 3\n\rTicks elapsed: ";
	int randomSeed = 1000;

	while(true){
		xEventGroupWaitBits(eventVar, firstBit, pdFALSE, pdFALSE, portMAX_DELAY);

		srand(randomSeed);
		randomSeed--;

		char tickCountStr[16] = {0};

		TickType_t temp = xTaskGetTickCount();
		vTaskDelay((rand() % 1000 + 1000));
		sprintf(tickCountStr, "%lu", xTaskGetTickCount() - temp);

		xSemaphoreTake(lock, portMAX_DELAY);

		USB_send((uint8_t*) taskStr, strlen(taskStr));
		USB_send((uint8_t*) tickCountStr, strlen(tickCountStr));

		xSemaphoreGive(lock);

		xEventGroupClearBits(eventVar, firstBit);
		xEventGroupSetBits(eventVar, fourthBit);
	}
}

int main(void) {

	prvSetupHardware();
	ITM_init();

	xTaskCreate(pressButton, "pressButton",
				configMINIMAL_STACK_SIZE * 3, NULL, (tskIDLE_PRIORITY + 2UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(waitForButton1, "waitForButton1",
				configMINIMAL_STACK_SIZE * 3, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(waitForButton2, "waitForButton2",
				configMINIMAL_STACK_SIZE * 3, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(waitForButton3, "waitForButton3",
				configMINIMAL_STACK_SIZE * 3, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(cdc_task, "cdc_task",
				configMINIMAL_STACK_SIZE * 3, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);


	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
