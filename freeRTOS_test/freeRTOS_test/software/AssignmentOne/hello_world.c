#include <stdio.h>
#include <system.h>
#include <stdlib.h>
#include <unistd.h>
#include "sys/alt_irq.h"
#include "altera_avalon_pio_regs.h"
#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/queue.h"
#include "altera_up_avalon_ps2.h"
#include "altera_up_ps2_keyboard.h"
#include "FreeRTOS/semphr.h"
#include "io.h"


#define Switch_Control_Param    ( ( void * ) 0x12345678 )
//#define mainTask_PRIORITY (tskIDLE_PRIORITY + 1)

#define Switch_Task_Priority 4
#define Stable_Mon_Tsk_Priority 6
#define Keyboard_Task_Priority 2
#define Load_Management_Task_Priority 5
#define Load_LED_Ctrl_Task_Priority 3
#define VGA_Task_Priority 1
#define   STBL_QUEUE_SIZE  1
// Global Variables:
int Current_Switch_State, Current_Freq, Freq_Val, Maintenance,
		Current_Stable, Thresh_Val, Thresh_ROC, Prev_Stable, Ld_Manage_State, Init_Load;
float Prev_Five_Freq[5], Current_ROC_Freq;
uint8_t use_ROC_Thresh = 0; // 0 is using Thresh_Val, 1 is using ROC_Thresh

// Semaphores:
SemaphoreHandle_t semaphore;
SemaphoreHandle_t freqSemaphore;
SemaphoreHandle_t threshold_mutex;

TaskHandle_t switch_control_handle;

// Queues
QueueHandle_t StabilityQ;



static void Load_Management_Task(void *pvParams);
static void Stability_Monitor_Task(void *pvParams);
static void Keyboard_Task(void *pvParams);
static void VGA_Task(void *pvParams);
static void Load_LED_Ctrl_Task(void *pvParams);
static void Switch_Control_Task(void *pvParams);
unsigned char byte;

void ps2_isr(void* ps2_device, alt_u32 id){
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	char ascii;
	int status = 0;
	KB_CODE_TYPE decode_mode;
	status = decode_scancode (ps2_device, &decode_mode , &byte , &ascii) ;
	if ( status == 0 ) //success
	{
		// print out the result
		switch ( decode_mode )
	    {
	      case KB_ASCII_MAKE_CODE :
	        //printf ( "ASCII   : %x\n", byte ) ;
	        break ;
	      case KB_LONG_BINARY_MAKE_CODE :
	        // do nothing
	      case KB_BINARY_MAKE_CODE :
	        //printf ( "MAKE CODE : %x\n", byte ) ;
	        break ;
	      case KB_BREAK_CODE :
	        // do nothing
	      default :
	        xSemaphoreGiveFromISR(semaphore, &xHigherPriorityTaskWoken);
	        break ;
	    }
	    IOWR(SEVEN_SEG_BASE,0 ,byte);
	  }


}

void freq_relay_isr() {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	Freq_Val= IORD(FREQUENCY_ANALYSER_BASE, 0); //it's in COUNT, not in HERTZ. to convert, do 16000/temp
	xSemaphoreGiveFromISR(freqSemaphore, &xHigherPriorityTaskWoken);
}

int main(void) {
	StabilityQ = xQueueCreate( STBL_QUEUE_SIZE, sizeof( void* ) );
	alt_up_ps2_dev * ps2_device = alt_up_ps2_open_dev(PS2_NAME);
	//Threshold values defined.
	Thresh_Val = 55;
	Thresh_ROC = 5;
	if(ps2_device == NULL){
		printf("can't find PS/2 device\n");
		return 1;
	}

	alt_up_ps2_enable_read_interrupt(ps2_device);
	alt_irq_register(PS2_IRQ, ps2_device, ps2_isr);
	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay_isr);
	//Create binary Semaphore
	semaphore = xSemaphoreCreateBinary();
	freqSemaphore = xSemaphoreCreateBinary();
	threshold_mutex = xSemaphoreCreateMutex(); //has a priority inheritance mechanism unlike binary semaphores.
	xTaskCreate(Switch_Control_Task, 'Switch', configMINIMAL_STACK_SIZE, Switch_Control_Param, Switch_Task_Priority, switch_control_handle);
	xTaskCreate(Load_LED_Ctrl_Task,'LEDs',configMINIMAL_STACK_SIZE,NULL,Load_LED_Ctrl_Task_Priority,NULL);
	xTaskCreate(Keyboard_Task, 'Keyboard', configMINIMAL_STACK_SIZE, NULL, Keyboard_Task_Priority, NULL);
	xTaskCreate(Stability_Monitor_Task, 'Monitoring', configMINIMAL_STACK_SIZE, NULL, Stable_Mon_Tsk_Priority, NULL);
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

static void Stability_Monitor_Task(void *paParams) {
	unsigned int stableQ;
	while (1) {
		float temp_five[5];
		if (xSemaphoreTake(freqSemaphore, portMAX_DELAY) == pdTRUE) {
			memcpy(temp_five,Prev_Five_Freq, 5*sizeof(float));
			for (uint8_t i=0;i<3;i++) {

				Prev_Five_Freq[i+1] = temp_five[i];
			}

			Prev_Five_Freq[0] = 16000.0/(double)Freq_Val;	//Freq_val is in count
			//printf("%f hz\n", 16000.0/(double)Freq_Val);
			//printf("Count number %d\n", Freq_Val);
			float change_freq = Prev_Five_Freq[0] - Prev_Five_Freq[1];
			//printf("change freq %f\n", change_freq);
			Current_ROC_Freq = (double)(change_freq * 16000)/ (double)Freq_Val;
			//printf("Rate of change is %f hz/s \n", Current_ROC_Freq);

			if ((Prev_Five_Freq[0] < Thresh_Val) || (abs(Current_ROC_Freq)>Thresh_ROC)) {
				printf("-----UNSTABLE----- \n");
				printf("Current Freq:%f, Thresh: %d, Current ROC: %f, Thresh: %d \n",Prev_Five_Freq[0],Thresh_Val,Current_ROC_Freq,Thresh_ROC);
				Current_Stable = 0;
			} else {
				Current_Stable = 1;
			}
			stableQ = Current_Stable;
			xQueueSend(StabilityQ, (void *)&stableQ,0);
			vTaskDelay(100);
		} else {
			printf("no freq_semaphore was taken");
		}
	}
}
static void Keyboard_Task(void *pvParams) {
	//Special bytes from keyboard:
	// UP:   75
	//DOWN:  72
	//LEFT:  6B
	//RIGHT: 74
	while (1) {
		if (xSemaphoreTake(semaphore, portMAX_DELAY) == pdTRUE) {
			if (byte == 0x75) {
				// Up case
				if (use_ROC_Thresh == 0) {
					Thresh_Val +=1;
					printf("%d\n",Thresh_Val);
				} else {
					Thresh_ROC += 1;
					printf("%d\n",Thresh_ROC);
				}

			} else if (byte == 0x72) {
				// DOWN case
				if (use_ROC_Thresh ==0) {
					Thresh_Val += -1;
					printf("%d\n",Thresh_Val);
				} else {
					Thresh_ROC += -1;
					printf("%d\n",Thresh_ROC);
				}
			} else if (byte == 0x6B || byte== 0x74) {
				use_ROC_Thresh = use_ROC_Thresh ^ 0b1;
			} else {
				printf("not valid key press \n");
			}
		} else {
			printf("no semaphore was taken");
		}
	}
}



