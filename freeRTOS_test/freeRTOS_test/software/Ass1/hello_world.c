/*
 * "Hello World" example.
 *
 * This example prints 'Hello from Nios II' to the STDOUT stream. It runs on
 * the Nios II 'standard', 'full_featured', 'fast', and 'low_cost' example
 * designs. It runs with or without the MicroC/OS-II RTOS and requires a STDOUT
 * device in your system's hardware.
 * The memory footprint of this hosted application is ~69 kbytes by default
 * using the standard reference design.
 *
 * For a reduced footprint version of this template, and an explanation of how
 * to reduce the memory footprint for a given application, see the
 * "small_hello_world" template.
 *
 */

#include <stdio.h>
#include <system.h>
#include <unistd.h>
#include "sys/alt_irq.h"
#include "altera_avalon_pio_regs.h"
#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/queue.h"
#include "altera_up_avalon_ps2.h"
#include "FreeRTOS/semphr.h"

#define Switch_Control_Param    ( ( void * ) 0x12345678 )
//#define mainTask_PRIORITY (tskIDLE_PRIORITY + 1)

#define Switch_Task_Priority 4
#define Stable_Mon_Tsk_Priority 6
#define Keyboard_Task_Priority 2
#define Load_Management_Task_Priority 5
#define Load_LED_Ctrl_Task_Priority 3
#define VGA_Task_Priority 1

int Current_Switch_State, Current_Freq, Freq_Val, Current_ROC_Freq, Maintenance,
		Current_Stable, Thresh_Val, Prev_Stable, Ld_Manage_State, Init_Load;
int Prev_Five_Freq[5];
SemaphoreHandle_t semaphore;
TaskHandle_t switch_control_handle;
//TaskHandle_t switch_control_handle;
//TaskHandle_t switch_control_handle;
//TaskHandle_t switch_control_handle;


static void Load_Management_Task(void *pvParams);
static void Stability_Monitor_Task(void *pvParams);
static void Keyboard_Task(void *pvParams);
static void VGA_Task(void *pvParams);
static void Load_LED_Ctrl_Task(void *pvParams);
static void Switch_Control_Task(void *pvParams);
unsigned char byte;

//Define ISR for keyboard input
void ps2_isr(void* ps2_device, alt_u32 id) {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	alt_up_ps2_read_data_byte_timeout(ps2_device, &byte);
	semaphore = xSemaphoreCreateBinary();
	xSemaphoreGiveFromISR(semaphore, &xHigherPriorityTaskWoken);
	//portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

int initOSDataStructs(void) {
	//A counter that counts down, where within that time, thats the amount of semaphore tokens can be grabbed
	return 0;
}

int main(void) {
	//initOSDataStructs();
	//xTaskCreate(Switch_Control_Task, 'Switch', configMINIMAL_STACK_SIZE, Switch_Control_Param, Switch_Task_Priority, switch_control_handle);
	//xTaskCreate(Load_LED_Ctrl_Task,'LEDs',configMINIMAL_STACK_SIZE,NULL,Load_LED_Ctrl_Task_Priority,NULL);
	xTaskCreate(Keyboard_Task, 'Keyboard', configMINIMAL_STACK_SIZE, NULL, Keyboard_Task_Priority, NULL);
	//xTaskCreate(Stability_Monitor_Task, 'Monitoring', configMINIMAL_STACK_SIZE, NULL, Stable_Mon_Tsk_Priority, NULL);
	//xTaskCreate(Load_Management_Task, 'Management', configMINIMAL_STACK_SIZE, NULL, Load_Management_Task_Priority, NULL);
	//xTaskCreate(VGA_Task, 'VGA', configMINIMAL_STACK_SIZE, NULL, VGA_Task_Priority, NULL);

	vTaskStartScheduler();
	for(;;);
}


static void Switch_Control_Task(void *pvParams) {
	while (1) {
		//printf("SwitchTask\n"); //Debug version
		Current_Switch_State = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);
		vTaskDelay(pdMS_TO_TICKS(200));
	}
}
static void Load_LED_Ctrl_Task(void *pvParams) {
	while (1) {
		//printf("LED Control Task \n");
		IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, Current_Switch_State);
		vTaskDelay(pdMS_TO_TICKS(200));
	}
}

static void Keyboard_Task(void *pvParams) {

	printf("Keyboard\n");
	alt_up_ps2_dev * ps2_device = alt_up_ps2_open_dev(PS2_NAME);
	printf("grabbed\n");
	printf("clearing buffer\n");
	//alt_up_ps2_clear_fifo (ps2_device) ;
	alt_up_ps2_enable_read_interrupt(ps2_device);
	printf("enabling read interrupt\n");
//	alt_irq_register(PS2_IRQ, ps2_device, ps2_isr);
//	printf("chuck in register\n");
	while (1) {

		if (xSemaphoreTake(semaphore, portMAX_DELAY) == pdTRUE) {
			printf("Scan code: %x\n", byte);
			printf("hallo crap\n");
			vTaskDelay(pdMS_TO_TICKS(200));
		}
		printf("Shit did not happen\n");
	}

}
//static void VGA_Task(void *pvParams) {
//}
