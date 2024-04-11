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
#include "FreeRTOS/timers.h"
#include "io.h"
#include "altera_up_avalon_video_character_buffer_with_dma.h"
#include "altera_up_avalon_video_pixel_buffer_dma.h"


#define Switch_Control_Param    ( ( void * ) 0x12345678 )
//#define mainTask_PRIORITY (tskIDLE_PRIORITY + 1)

#define Switch_Task_Priority 4
#define Stable_Mon_Tsk_Priority 1
#define Keyboard_Task_Priority 5
#define Load_Management_Task_Priority 2
#define Load_LED_Ctrl_Task_Priority 3
#define VGA_Task_Priority 6
#define   STBL_QUEUE_SIZE  50

//For frequency plot
#define FREQPLT_ORI_X 101		//x axis pixel position at the plot origin
#define FREQPLT_GRID_SIZE_X 50	//pixel separation in the x axis between two data points
#define FREQPLT_ORI_Y 199.0		//y axis pixel position at the plot origin
#define FREQPLT_FREQ_RES 20.0	//number of pixels per Hz (y axis scale)

#define ROCPLT_ORI_X 101
#define ROCPLT_GRID_SIZE_X 50
#define ROCPLT_ORI_Y 259.0
#define ROCPLT_ROC_RES 0.5		//number of pixels per Hz/s (y axis scale)

#define MIN_FREQ 45.0 //minimum frequency to draw

// Global Variables:
int Current_Switch_State, Current_Freq, Freq_Val, Maintenance,
		Current_Stable, Thresh_Val, Thresh_ROC, Prev_Stable, Ld_Manage_State, Init_Load, LoadTimeExp;
float Prev_Five_Freq[5], Current_ROC_Freq[5];
uint8_t use_ROC_Thresh = 0; // 0 is using Thresh_Val, 1 is using ROC_Thresh

// Semaphores:
SemaphoreHandle_t semaphore;
SemaphoreHandle_t freqSemaphore;
SemaphoreHandle_t threshold_mutex;

TaskHandle_t switch_control_handle;
TaskHandle_t PRVGADraw;

// Queues
QueueHandle_t StabilityQ;
QueueHandle_t LoadControlQ;
static QueueHandle_t Q_freq_data;

// Timers
TimerHandle_t LoadTimer;

typedef struct{
	unsigned int x1;
	unsigned int y1;
	unsigned int x2;
	unsigned int y2;
}Line;

static void Load_Management_Task(void *pvParams);
static void Stability_Monitor_Task(void *pvParams);
static void Keyboard_Task(void *pvParams);
static void VGA_Task(void *pvParams);
static void Load_LED_Ctrl_Task(void *pvParams);
static void Switch_Control_Task(void *pvParams);
unsigned char byte;


void MaintenanceStateButton () {

	  // Inverting maintenance mode:
	Maintenance ^= 0x1;
	  // clears the edge capture register
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);

}
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
void loadTimerISR(TimerHandle_t xTimer) {
	// Timer has expired, do smthn
	printf("expired \n");
	LoadTimeExp = 1;
}
void freq_relay_isr() {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	Freq_Val= IORD(FREQUENCY_ANALYSER_BASE, 0); //it's in COUNT, not in HERTZ. to convert, do 16000/temp
	//double temp = 16000.0/Freq_Val;
	//xQueueSendToBackFromISR(Q_freq_data, &temp, pdFALSE);
	xSemaphoreGiveFromISR(freqSemaphore, &xHigherPriorityTaskWoken);
}

//===================================MAIN=============================================//
int main(void) {
	StabilityQ = xQueueCreate( STBL_QUEUE_SIZE, sizeof( void* ) );
	LoadControlQ = xQueueCreate(STBL_QUEUE_SIZE, sizeof(void*));
	//Q_freq_data = xQueueCreate(5, sizeof(double) );
	alt_up_ps2_dev * ps2_device = alt_up_ps2_open_dev(PS2_NAME);

	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7); // Clearing edge capture register for ISR
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE,0x7); // Enabling interrupts for the buttons
	alt_irq_register(PUSH_BUTTON_IRQ,0,MaintenanceStateButton);
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


	// Creating Timers
	LoadTimer = xTimerCreate("LoadTimer",pdMS_TO_TICKS(500),pdTRUE,NULL,loadTimerISR);

	// Creating Tasks used
	xTaskCreate(Switch_Control_Task, 'Switch', configMINIMAL_STACK_SIZE, Switch_Control_Param, Switch_Task_Priority, switch_control_handle);
	xTaskCreate(Load_LED_Ctrl_Task,'LEDs',configMINIMAL_STACK_SIZE,NULL,Load_LED_Ctrl_Task_Priority,NULL);
	xTaskCreate(Keyboard_Task, 'Keyboard', configMINIMAL_STACK_SIZE, NULL, Keyboard_Task_Priority, NULL);
	xTaskCreate(Stability_Monitor_Task, 'Monitoring', configMINIMAL_STACK_SIZE, NULL, Stable_Mon_Tsk_Priority, NULL);
	xTaskCreate(Load_Management_Task, 'LoadShed',configMINIMAL_STACK_SIZE,NULL,Load_Management_Task_Priority,&PRVGADraw);

	xTaskCreate(VGA_Task, 'VGA', configMINIMAL_STACK_SIZE, NULL, VGA_Task_Priority, NULL);

	vTaskStartScheduler();
	for(;;);
}

//=======================================END OF MAIN==================================================================//
static void Switch_Control_Task(void *pvParams) {
	while (1) {
		printf("SwitchTask\n"); //Debug version
		Current_Switch_State = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);
		vTaskDelay(pdMS_TO_TICKS(200));
	}
}
static void Load_LED_Ctrl_Task(void *pvParams) {
	unsigned int *LdQ;
	while (1) {
		printf("LED Control Task \n");
		xQueueReceive(LoadControlQ,&LdQ,portMAX_DELAY);
		IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, Current_Switch_State);
		IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE,LdQ);
		vTaskDelay(pdMS_TO_TICKS(200));
	}
}
static void Load_Management_Task(void *pvParams) {
	unsigned int *stable;
	unsigned int LoadQ;
	while(1) {

		xQueueReceive(StabilityQ,&stable,portMAX_DELAY);

		if (Maintenance != 0) {
			if (stable == 0 && Ld_Manage_State == 0) { // Found to be unstable
			// Labeling as now in load managing state
				Ld_Manage_State = 1;

				Init_Load = Current_Switch_State;
				Prev_Stable = Current_Switch_State;
				xTimerStart(LoadTimer,0);
			}
			if (Ld_Manage_State == 1) {
				if (stable ==1) {
					if ((Prev_Stable >> 5) == 0) { // If it was unstable prior we reset the timer
						xTimerReset(LoadTimer,0);
					} else if (LoadTimeExp == 1) {
						LoadTimeExp = 0;
						// Restoring

						//loop through and check to see which of the highest bits need to be 'unshedded'
						int found = 0;
						int pos = 4;
						int mask = 1;
						int val;
						printf("Prev_Stable: %d\n",Prev_Stable);
						while (found == 0 && pos>=0) {
							mask = 1<<pos;
							val = Prev_Stable & mask;
							if (!val && (Init_Load & (1U << pos))) {
								found =1;
								printf("Position:%d \n",pos);
								printf("Init: %d\n",Init_Load);
								break;
							}
							pos--;
						}
						if (found == 1) {
							Prev_Stable |= (1U << pos); // putting 1 into the position found
							LoadQ = Prev_Stable;
							LoadQ &= 0b011111;
							printf("LOAD: %d\n",LoadQ);
							xQueueSend(LoadControlQ,(void *)&LoadQ,0);

						}
						xTimerStart(LoadTimer,0);
					}
					Prev_Stable |= (1U << 5);
					if (Prev_Stable == (Init_Load | 1<<5)) {
						// No longer needs to be in load manage state
						Ld_Manage_State = 0;
					}

				} else {
					printf("UNSABLE \n");
					// Finding the lowest bit (lowest priority that is on)
					int pos = 0;
					unsigned int mask = 1;
					if ((Prev_Stable >> 5) == 1) { // If it was stable prior we reset the timer
						xTimerReset(LoadTimer,0);

					} else if ((LoadTimeExp == 1) || ((Prev_Stable & 0b011111) == Init_Load) ) {

						LoadTimeExp = 0;
						int iso = Prev_Stable & -Prev_Stable;
						while (iso > 1) {
							iso>>=1;
							pos++;
						}
						if (pos == 5) {
							// ALL LOADS ARE OFF, don't do any shedding
						} else {
							mask = 1 << pos;
							Prev_Stable &= ~mask;
							LoadQ = Prev_Stable;
							LoadQ &= 0b011111;
							printf("LOAD: %d\n",LoadQ);
							xQueueSend(LoadControlQ,(void *)&LoadQ,0);

						}
						Prev_Stable = (Prev_Stable & ~(1 << 5)) | (0<< 5);
						xTimerStart(LoadTimer,0);
					}

				}

			}
		} else {
			LoadQ = Current_Switch_State;
			xQueueSend(LoadControlQ,(void *)&LoadQ,0);
			Ld_Manage_State = 0;
		}
		vTaskDelay(200);

	}
}
static void Stability_Monitor_Task(void *paParams) {
	unsigned int stableQ;
	while (1) {
		float temp_five[5];
		if (xSemaphoreTake(freqSemaphore, portMAX_DELAY) == pdTRUE) {
			memcpy(temp_five,Prev_Five_Freq, 5*sizeof(float));
			for (uint8_t i=0;i<4;i++) {
				Prev_Five_Freq[i+1] = temp_five[i];
			}
			memcpy(temp_five,Current_ROC_Freq, 5*sizeof(float));
			for (uint8_t i=0;i<4;i++) {
				Current_ROC_Freq[i+1] = temp_five[i];
			}

			Prev_Five_Freq[0] = 16000.0/(double)Freq_Val;	//Freq_val is in count
			//printf("%f hz\n", 16000.0/(double)Freq_Val);
			//printf("Count number %d\n", Freq_Val);
			float change_freq = Prev_Five_Freq[0] - Prev_Five_Freq[1];
			//printf("change freq %f\n", change_freq);
			Current_ROC_Freq[0] = (double)(change_freq * 16000)/ (double)Freq_Val;
			//printf("Rate of change is %f hz/s \n", Current_ROC_Freq);

			if ((Prev_Five_Freq[0] < Thresh_Val) || (abs(Current_ROC_Freq)>Thresh_ROC)) {
				//printf("-----UNSTABLE----- \n");
				//printf("Current Freq:%f, Thresh: %d, Current ROC: %f, Thresh: %d \n",Prev_Five_Freq[0],Thresh_Val,Current_ROC_Freq,Thresh_ROC);
				Current_Stable = 0;
				stableQ = Current_Stable;
				xQueueSend(StabilityQ, (void *)&stableQ,0);
			} else {
				Current_Stable = 1;
				stableQ = Current_Stable;
				xQueueSend(StabilityQ, (void *)&stableQ,0);
			}
			//printf("Stable: %d \n",stableQ);
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
					if (Thresh_Val <= 0) {
						Thresh_Val = 0;
					}
					printf("%d\n",Thresh_Val);
				} else {
					Thresh_ROC += -1;
					if (Thresh_ROC <= 0) {
						Thresh_ROC = 0;
					}
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

void VGA_Task(void *pvParameters ){


	//initialize VGA controllers
	alt_up_pixel_buffer_dma_dev *pixel_buf;
	pixel_buf = alt_up_pixel_buffer_dma_open_dev(VIDEO_PIXEL_BUFFER_DMA_NAME);
	if(pixel_buf == NULL){
		printf("can't find pixel buffer device\n");
	}
	alt_up_pixel_buffer_dma_clear_screen(pixel_buf, 0);

	alt_up_char_buffer_dev *char_buf;
	char_buf = alt_up_char_buffer_open_dev("/dev/video_character_buffer_with_dma");
	if(char_buf == NULL){
		printf("can't find char buffer device\n");
	}
	alt_up_char_buffer_clear(char_buf);



	//Set up plot axes
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 400, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 400, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 50, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 220, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);

	alt_up_char_buffer_string(char_buf, "Frequency(Hz)", 4, 4);
	alt_up_char_buffer_string(char_buf, "52", 10, 7);
	alt_up_char_buffer_string(char_buf, "50", 10, 12);
	alt_up_char_buffer_string(char_buf, "48", 10, 17);
	alt_up_char_buffer_string(char_buf, "46", 10, 22);

	alt_up_char_buffer_string(char_buf, "df/dt(Hz/s)", 4, 26);
	alt_up_char_buffer_string(char_buf, "60", 10, 28);
	alt_up_char_buffer_string(char_buf, "30", 10, 30);
	alt_up_char_buffer_string(char_buf, "0", 10, 32);
	alt_up_char_buffer_string(char_buf, "-30", 9, 34);
	alt_up_char_buffer_string(char_buf, "-60", 9, 36);
	alt_up_char_buffer_string(char_buf, "Lower Threshold", 10, 45);
	alt_up_char_buffer_string(char_buf, "RoC Threshold", 10, 48);



	int i = 0, j = 0;
	Line line_freq, line_roc;
	char buffer1[50];
	char buffer2[50];
	while(1){

		//receive frequency data from queue
		printf("VGA\n");
		//clear old graph to draw new graph
		sprintf(buffer1, "%d", Thresh_Val);
		sprintf(buffer2, "%d", Thresh_ROC);
		alt_up_char_buffer_string(char_buf, buffer1 , 30, 45);
		alt_up_char_buffer_string(char_buf, buffer2 , 30, 48);
		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 0, 639, 199, 0, 0);
		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 201, 639, 299, 0, 0);

		for(j=0;j<5;++j){ //i here points to the oldest data, j loops through all the data to be drawn on VGA
			if (((int)(Prev_Five_Freq[(i+j)%5]) > MIN_FREQ) && ((int)(Prev_Five_Freq[(i+j+1)%5]) > MIN_FREQ)){
				//Calculate coordinates of the two data points to draw a line in between
				//Frequency plot
				line_freq.x1 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * j;
				line_freq.y1 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (Prev_Five_Freq[(i+j)%5] - MIN_FREQ));

				line_freq.x2 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * (j + 1);
				line_freq.y2 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (Prev_Five_Freq[(i+j+1)%5] - MIN_FREQ));

				//Frequency RoC plot
				line_roc.x1 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * j;
				line_roc.y1 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * Current_ROC_Freq[(i+j)%5]);

				line_roc.x2 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * (j + 1);
				line_roc.y2 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * Current_ROC_Freq[(i+j+1)%5]);

				//printf("first: %f\n", Prev_Five_Freq[0]);
				//printf("last value: %f\n", Prev_Five_Freq[4]);
				//Draw
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_freq.x1, line_freq.y1, line_freq.x2, line_freq.y2, 0x3ff << 0, 0);
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_roc.x1, line_roc.y1, line_roc.x2, line_roc.y2, 0x3ff << 0, 0);
			}
		}

		vTaskDelay(pdMS_TO_TICKS(500));

	}
}



