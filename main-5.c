/*
 * Lab 5
 * Author: Trenton
 * Date: 02/28/25
 * Description: Implement multithreading to handle interrupts
 * The main program loop counts from 1 to 60 seconds,
 * 	prints each value to the LCD, registers DI, and creates a new task.
 * A second thread handles ISR.
 * An external digital interrupt, a debounced switch, signals
 * 	an interrupt to the program and causes "interrupt_" to print
 * 	to an LCD without the main count time being affected.
 */

/* includes */
#include <stdio.h>
#include "MyRio.h"
#include "T1.h"
#include "DIIRQ.h"		// Lab 5 specific, threads
#include <pthread.h>	// Lab 5 specific, threads

/* prototypes */
//pthread prototypes included in pthread.h
void wait5(void);		// 5 ms wait
void countloop(int);	// waits 1 second and prints count
void* DI_ISR(void *thread_resource);

//Globally defined thread resource structure
typedef struct{
	NiFpga_IrqContext irqContext;	// IRQ context reserved
	NiFpga_Bool irqThreadRdy;		// IRQ thread ready flag
	uint8_t irqNumber;				// IRQ number value
} ThreadResource;

// main program loop #############################################################
int main(int argc, char **argv){
/* Description of main()
 *
1) Open the myRIO session.
2) Register the digital input (DI) interrupt.
3) Create an interrupt thread to service the interrupt.
4) Begin a loop. Each time through the loop, the following happens:
	Wait 1 s by calling the (5 ms) wait() function 200 times.
	Clear the display and print the value of the count.
	Increment the value of the count.
5) After a count of 60, signal the interrupt thread to stop,
	and wait until it terminates.
6) Unregister the interrupt.
7) Close the myRIO session.*/

	// 1) MyRio session open - required by hardware-------------------------------
	NiFpga_Status status;							// declare status type
	status = MyRio_Open();		    				// open FPGA session
	if (MyRio_IsNotSuccess(status)) return status;	// test if session opened

	// Lab 5 Code		 ---------------------------------------------------------

	// 2) Register the digital input (DI) interrupt ------------------------------
	int32_t irq_status;
	uint8_t irqNumber = 2;

	// Specify  IRQ channel settings
	MyRio_IrqDi irqDI0;
	irqDI0.dioCount          = IRQDIO_A_0CNT;
	irqDI0.dioIrqNumber      = IRQDIO_A_0NO;
	irqDI0.dioIrqEnable      = IRQDIO_A_70ENA;
	irqDI0.dioIrqRisingEdge  = IRQDIO_A_70RISE;
	irqDI0.dioIrqFallingEdge = IRQDIO_A_70FALL;
	irqDI0.dioChannel        = Irq_Dio_A0;

	// Declare the thread resource, and set the IRQ number
	ThreadResource irqThread0;
	irqThread0.irqNumber = irqNumber;

	// Register DI0 IRQ. Terminate if not successful
	irq_status = Irq_RegisterDiIrq(&irqDI0,
	                               &(irqThread0.irqContext),
	                               irqNumber,              // IRQ Number
	                               1,                      // Count
	                               Irq_Dio_FallingEdge);   // TriggerType

	// Set the ready flag to enable the new thread
	irqThread0.irqThreadRdy = NiFpga_True;

	// 3) Create interrupt thread for function ---------------------------------
	pthread_t thread;
	irq_status = pthread_create(&thread,
	                            NULL,                      // attr default ok
	                            DI_ISR,                    // start routine
	                            &irqThread0);

	// 4) 1-60 second Count Loop -----------------------------------------------
	int i;
	static int time_max = 60; // seconds
	for (i=1; i<=time_max; i++){
		countloop(i);
	}

	// 5,6) Terminate ISR and unregister interrupt -----------------------------
	irqThread0.irqThreadRdy = NiFpga_False;	// set flag to false, signals thread end
	irq_status = pthread_join(thread, NULL);			// join threads
	irq_status = Irq_UnregisterDiIrq(&irqDI0,
								irqThread0.irqContext,
								irqThread0.irqNumber);

	// 7) MyRio session close - required by hardware ---------------------------
	status = MyRio_Close();						// close FPGA session
	return status;								// return status of session
}	// end of main()

// Functions ###################################################################

// ISR Function
void* DI_ISR(void *thread_resource){
	/*
	 * 1) Cast the thread resource
	 * 2) service DI interrupts until signaled to stop
	 * 		-falling edge interrupts
	 * 		-irqThreadRdy flag, false -> stop
	 * 3) terminate thread (itself)
	 */

	// 1) Cast the thread resource
	ThreadResource *threadResource = (ThreadResource*) thread_resource;

	// 2) service interrupts until signaled to stop
	while (threadResource->irqThreadRdy == NiFpga_True) {
		uint32_t irqAssert = 0;
		// pause the loop while waiting for the interrupt
		Irq_Wait(threadResource->irqContext,
		         threadResource->irqNumber,
		         &irqAssert,
		         (NiFpga_Bool*) &(threadResource->irqThreadRdy));
		// scheduler acknowledgement
		if (irqAssert & (1 << threadResource->irqNumber)) {
			/*  ISR code: print "interrupt_" to LCD */
			printf_lcd("\finterrupt_");
			Irq_Acknowledge(irqAssert);

			/* Test code to check debounce
			*static int m = 1;
			*printf("\ninterrupt_%d",m);
			*m++;
			*/
		}
	}

	// 3) terminate thread (itself)
	pthread_exit(NULL);
	return NULL;
}

void countloop(i){
	/*
	 * Performs 1 second wait and prints current count
	 */
	int k;
	// wait 1 second: 5ms wait * 200
	for (k=0; k<200; k++){
		wait5();
	}
	printf_lcd("\f%d",i); // print count value
}

void wait5(void){
	/*
	 * This function waits for the calibrated amount of milliseconds: 5ms.
	 */
	uint32_t i;
	i = 417000;
	while (i>0){
		i--;
	}
	return;
}



