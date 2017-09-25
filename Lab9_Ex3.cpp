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
EventGroupHandle_t eventVar = xEventGroupCreate();

#define firstBit (1 << 0)
#define secondBit (1 << 1)
#define thirdBit (1 << 2)
#define watchdogBit (1 << 3)

static void monitorSW1 (void *pvParameters){
	DigitalIoPin sw1(0, 17, DigitalIoPin::pullup, true);

	while(true){
		if(sw1.read()){
			xEventGroupSetBits(eventVar, firstBit);
			xEventGroupWaitBits(eventVar, watchdogBit, pdTRUE, pdTRUE, portMAX_DELAY);
		}
		while(sw1.read());
		vTaskDelay(configTICK_RATE_HZ/10);
	}
}

static void monitorSW2 (void *pvParameters){
	DigitalIoPin sw2(1, 11, DigitalIoPin::pullup, true);

	while(true){
		if(sw2.read()){
			xEventGroupSetBits(eventVar, secondBit);
			xEventGroupWaitBits(eventVar, watchdogBit, pdTRUE, pdTRUE, portMAX_DELAY);
		}
		while(sw2.read());
		vTaskDelay(configTICK_RATE_HZ/10);
	}
}

static void monitorSW3 (void *pvParameters){
	DigitalIoPin sw3(1, 9, DigitalIoPin::pullup, true);

	while(true){
		if(sw3.read()){
			xEventGroupSetBits(eventVar, thirdBit);
			xEventGroupWaitBits(eventVar, watchdogBit, pdTRUE, pdTRUE, portMAX_DELAY);
		}
		while(sw3.read());
		vTaskDelay(configTICK_RATE_HZ/10);
	}
}

static void watchdogSW (void *pvParameters){
	TickType_t temp = 0;
	char tickCountStr[16] = {0};

	while(true){
			xEventGroupWaitBits(eventVar, firstBit | secondBit | thirdBit, pdFALSE, pdTRUE, configTICK_RATE_HZ*5);

			if(((xEventGroupGetBitsFromISR(eventVar) & (firstBit | secondBit | thirdBit)) == (firstBit | secondBit | thirdBit))){
				sprintf(tickCountStr, "%lu\n\r", xTaskGetTickCount() - temp);

				ITM_write("\n\rOK\n\r");
				ITM_write(tickCountStr);

				temp = xTaskGetTickCount();
			}else{
				if((xEventGroupGetBits(eventVar) & firstBit) != firstBit) {
					ITM_write("\n\rTask 1: fail");
				}

				if((xEventGroupGetBits(eventVar) & secondBit) != secondBit) {
					ITM_write("\n\rTask 2: fail");
				}

				if((xEventGroupGetBits(eventVar) & thirdBit) != thirdBit) {
					ITM_write("\n\rTask 3: fail");
				}

				sprintf(tickCountStr, "%lu\n\r", xTaskGetTickCount() - temp);
				ITM_write("\n\rTick count: ");
				ITM_write(tickCountStr);

				temp = xTaskGetTickCount();
			}

		xEventGroupClearBits(eventVar, firstBit | secondBit | thirdBit);
		xEventGroupSetBits(eventVar, watchdogBit);
	}
}

int main(void) {

	prvSetupHardware();
	ITM_init();

	xTaskCreate(monitorSW1, "monitorSW1",
				configMINIMAL_STACK_SIZE * 2, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(monitorSW2, "monitorSW2",
				configMINIMAL_STACK_SIZE * 2, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(monitorSW3, "monitorSW3",
				configMINIMAL_STACK_SIZE * 2, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(watchdogSW, "watchdogSW",
				configMINIMAL_STACK_SIZE * 6, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);


	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
