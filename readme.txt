Readme - Hello World Software Example

DESCRIPTION:
This is a program containing our solution for implementing the Low-Cost Frequency Relay (LCFR) of assignment 1 for COMPSYS 723 in embedded C. It is built on the Intel (Altera) NIOS II processor, embedded within the Cyclong FPGA on the DE2-115 board. It has 5 tasks:
-StabilityMonitorTask:

-LoadManagementTask:
	This task manages the load operations based on the system stability determined in 	SwitchMonitorTask and the maintenance mode determined by the user pressing a 	button. The task relies on a queue from StabilityMonitorTask that returns whether 	the signal is stable or not. These values will be used in the decision making to 	determine whether the loads should shed, unshed or wait. 

-VGATask:
	This task is responsible for displaying the following information onto the screen 	through VGA:
	*The last 5 recorded frequency values and a graphical representation of it
	*The last 5 recorded rate of change frequency values a graphical representation of 	 it
	*Maintenance mode
	*Average time of the 5 recorded frequency values
	*Average time of the 5 recorded rate of change frequency values
	*The minimum frequency value recorded
	*The maximum frequency recorded
	*The minimum rate of change frequency value recorded
	*The maximum rate of change frequency value recorded
	*The total time the system is running

-StabilityMonitorTask:
	This task is responsible monitioring the ADC count values received from the 	Frequency analyser, which are used for calculating the frequency and rate of change 	of frequency and using these two values to determine whether it violated any of the 	conditions that makes the system unstable. These conditions are under-frequency and 	excessive rate of change determined by a preset threshold for both frequency and 	rate of change frequency. 
	  

-LoadLEDControl:
	This task is responsible for managing the LED indicators for load status, updating 	the LED settings in response to change in load condiitons, which depends on which	loads are shedded or operating. 

-KeyboardTask:
	This task is responsible for the user interaction with the keyboard, where by 	pressing the up or down arrow keys, the user can increase or decrease the preset 	thresholds for either frequency or rate of change frequency, that is determined by 	the user pressing either the left arrow for frequency or right arrow for changing 	rate of change frequency. 

-SwitchControlTask:
	This task monitors the load switches made by the user which it updates using a 	periodic of 200ms refreshes. This task takes into account the current state of the 	system, ensuring new loads will not affect the loads while it is in load managing. 


PERIPHERALS USED:
We used the following peripherals for our solution:
-PS/2 Keyyboard
-DE2-115 development board
-VGA cable
-Monitor


SOFTWARE SOURCE FILES:
This solution includes the following software source files:
- hello_world.c

BOARD/HOST REQUIREMENTS:
Our solution requires the user to have Quartus Prime v18.1 to be installed onto their devices, where the Cyclone IV family is installed within Quartus. If the DE2-115 board cannot be detected on the device, they must check whether it's connected correctly. If it is connected correctly, check whether the USB blaster driver is installed. 
