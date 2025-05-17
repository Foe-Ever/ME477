/*
 * Lab 6
 * Author: Trenton
 * Date: 03/07/25
 * Description: RTC interrupts, biquad cascade, ADC/DAC, matlab simulation
 * 1) Employ a real time clock interrupt for precise timing. 2 threads.
 * 2) Implement a transfer function generator, using Tustin approximations
 * 	and biquad cascades. Difference equation approximation to a linear diff. eq.
 * 3) ADC and DAC conversion using MyRio to read input waves and
 * 	output an comparable waveform.
 * 4) output data to a matlab file and compare the values to a simulated
 * 	continuous time transfer system (via matlab).
 */

/* includes -------------------------------------------------------*/
#include <stdio.h>
#include "MyRio.h"
#include "T1.h"

#include "AIO.h"		// analog input and output for MyRio
#include <pthread.h>	// linux multithreading
#include "TimerIRQ.h"	// timer irq for interrupt method
#include "matlabfiles.h"// matlab file creation

//#include "emulate.h"	// emulated analog input for matlab file

/* biquad structure for cascade()*/
struct biquad {
  double b0; double b1; double b2;   // numerator
  double a0; double a1; double a2;   // denominator
  double x0; double x1; double x2;   // input
  double y1; double y2;              // output
};

/* prototypes  -------------------------------------------------------*/

/* These prototypes are included in headers.
 * Listed here for my own reference.

// register timer irq and its inputs prototypes are in TimerIRQ.h
// pthread prototypes in pthread.h
//char getkey(void);	// in T1.h
// AIO channels, provided in T1 include
//void Aio_InitCI0(MyRio_Aio *AIC0);	// Input 0
//void Aio_InitCO0(MyRio_Aio *AOC0);	// Output 0
//void Aio_InitCO1(MyRio_Aio *AOC1);	// Output 1
// Read analog input, provided in AIO.h header
//double Aio_Read(MyRio_Aio *channel);
*/

// ISR and interrupt scheduler
void* Timer_ISR(void *thread_resource);

// biquad cascade implementation
double cascade(double xin,         // input
               struct biquad *fa,  // biquad array
               int    ns,          // no. segments
               double ymin,        // min output
               double ymax);       // max output

/* definitions and macros----------------------------------------------*/

//Globally defined thread resource structure
typedef struct{
	NiFpga_IrqContext irqContext;	// IRQ context reserved
	NiFpga_Bool irqThreadRdy;		// IRQ thread ready flag
} ThreadResource;

NiFpga_Session myrio_session;	// myrio session macro required for book code template

// saturation macro for cascade(), provided by book
#define SATURATE(x,lo,hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))

// MATLAB code
#define IMAX 500				//max points



// main program loop #############################################################
int main(int argc, char **argv){
/* Description of main()
 *	main() initializes our program and creates the thread that Timer_ISR operates on.
 *	A while loop controls the program's runtime, pressing "<-" on the keypad
 *		signals for the ISR thread to shutdown, threads are cleaned,
 *		then the whole program terminates.
 *	I added lcd output to signal to the user the condition of the program.
 *		(running vs. off)
 *
1) Open the myRIO session.
2) initialize analog channels on connector C
3) Set up Time IRQ interrupt thread
4) enter a loop until "<-" is pressed on the keypad (use getkey() )
5) After loop end, signal timer thread to terminate using irqThreadRdy flag
6) Unregister the interrupt.
7) Close myRIO session.
*/

	// 1) myRIO session open - required by hardware-------------------------------
	NiFpga_Status status;							// declare status type
	status = MyRio_Open();		    				// open FPGA session
	if (MyRio_IsNotSuccess(status)) return status;	// test if session opened

	// 3) configure timer interrupt and create timer thread ----------------------
	int32_t irq_status;
	MyRio_IrqTimer irqTimer0;
	ThreadResource irqThread0;
	pthread_t thread;
	irqTimer0.timerWrite = IRQTIMERWRITE;	// irq channel registers
	irqTimer0.timerSet = IRQTIMERSETTIME;
	uint32_t timeoutValue = 500;			// (micro seconds)
	irq_status = Irq_RegisterTimerIrq(&irqTimer0,
										&irqThread0.irqContext,
										timeoutValue);
	// set indicator to allow new thread
	irqThread0.irqThreadRdy = NiFpga_True;
	// create thread calling Timer_ISR()
	irq_status = pthread_create(&thread, NULL, Timer_ISR, &irqThread0);

	// 4) enter main loop --------------------------------------------------------
	printf_lcd("\fRunning\n\nTo stop: <- key"); // Signal program start to user

	// while "<-" hasn't been pressed on the keypad, loop (let ISR run)
	while ( (getkey() != DEL) ){}

	// ) Terminate ISR and unregister interrupt -----------------------------
	irqThread0.irqThreadRdy = NiFpga_False;		// set flag to false, signals thread end
	irq_status = pthread_join(thread, NULL);	// join threads
	irq_status = Irq_UnregisterTimerIrq(&irqTimer0, irqThread0.irqContext);

	// Signal program end
	printf_lcd("\fOff");

	// ) MyRio session close - required by hardware ---------------------------
	status = MyRio_Close();						// close FPGA session
	return status;								// return status of session
}// end of main()

// Functions ###################################################################

void* Timer_ISR(void *thread_resource){
/* Description of Timer_ISR
 * This function implements a biquad cascade to calculate an output value given
 * an input value on a thread separate from main.
 * An rtc timer interrupt triggers every 0.5ms to signal that cascade() should be executed.
 * 500 inputs and outputs are saved to a matlab file.
 *
 * 1) initialize
 * 	a) cast thread resource
 * 	b) AIO
 * 	c) set analog output connector AOC1 to 0V.
 * 	d) initialize cascade parameters
 * 2) Loop while irqThreadRdy is true
 * 	a) wait for IRQ to assert, write time interval to IRQTIMERWRITE
 * 		write TRUE to IRQTIMERSETTIME
 * 	b) read analog input AIC0 for x(n) value
 * 	c) call cascade() to calculate y(n) via biquad cascade
 * 	d) send y(n) to AOC1
 * 	e) Acknowledge interrupt
 * 3)Save 500 point response buffer to Lab6.mat file
 */

	// 1) Initialize: cast input resource
	ThreadResource *threadResource = (ThreadResource*) thread_resource;

	// variable declarations
	double v_in;
	double v_out;	// (volts)

	// matlab requirements
	static double buffer1[IMAX];	//v_in
	static double buffer2[IMAX];	//v_out
	static double *bp_in = buffer1;	//buffer pointer
	static double *bp_out = buffer2;	//buffer pointer

	// initialize analog i/o, connector C
	MyRio_Aio AIC0;		// C, analog input 0
	MyRio_Aio AOC1;		// C, analog output 1
	Aio_InitCI0(&AIC0);	// initialize i0
	Aio_InitCO1(&AOC1);	// initialize o1

	Aio_Write(&AOC1, 0);// start at 0V output
	// voltage is maintained until updated with another Aio_Write()

	// set cascade() parameters
	double v_min = -10;	// minimum saturation voltage (v)
	double v_max = 10;	// maximum saturation voltage (v)
	int myFilter_ns = 2;			// # of biquad sections
	uint32_t timeoutValue = 500;	// T - us; f_s = 2000 Hz, (500=0.5ms)
	static struct biquad myFilter[] = {
	  {1.0000e+00,  9.9999e-01, 0.0000e+00,
	   1.0000e+00, -8.8177e-01, 0.0000e+00, 0, 0, 0, 0, 0},
	  {2.1878e-04,  4.3755e-04, 2.1878e-04,
	   1.0000e+00, -1.8674e+00, 8.8220e-01, 0, 0, 0, 0, 0}
	};

	// 2) while loop to process interrupts, checks irqThreadRdy -----------------
	while (threadResource->irqThreadRdy == NiFpga_True){
		//wait for interrupt
		uint32_t irqAssert = 0;
		Irq_Wait(threadResource->irqContext,
				TIMERIRQNO,
				&irqAssert,
				(NiFpga_Bool*)&(threadResource->irqThreadRdy));
		// check for timer IRQ assert
		if (irqAssert & (1<<TIMERIRQNO)){
			// Schedule next interrupt
			NiFpga_WriteU32(myrio_session, IRQTIMERWRITE, timeoutValue);
			NiFpga_WriteBool(myrio_session, IRQTIMERSETTIME, NiFpga_True);

			//ISR service code --------------------------------------------------
			v_in = Aio_Read(&AIC0);	// Analog input voltage reading (volts)
			// run cascade() to calculate y(n), aka v_out
			v_out = cascade( v_in, myFilter, myFilter_ns, v_min, v_max);
			Aio_Write(&AOC1, v_out);	// write A0 voltage

			// matlab buffer
			if (bp_in < buffer1 + IMAX) {
					*bp_in++ = v_in;
					*bp_out++ = v_out;
				}


			Irq_Acknowledge(irqAssert);	// acknowledge interrupt
		}
	}

	//save matlab file
	int err=101;			// Error code
	MATFILE *mf;
	mf = openmatfile("Lab6_trenton_sine.mat", &err);	// open file
	if(!mf) printf("Can't open mat file %d\n", err);
	matfile_addstring(mf, "myName", "Trenton Fletcher");
	matfile_addmatrix(mf, "vin", buffer1, IMAX, 1, 0);
	matfile_addmatrix(mf, "vout", buffer2, IMAX, 1, 0);
	matfile_close(mf);		// close file

	Aio_Write(&AOC1, 0);// for safety, set output voltage to 0 volts
	pthread_exit(NULL); // exit thread
	return NULL;
}

double cascade(double xin, struct biquad *fa, int ns, double ymin, double ymax){
/*
 * Cascade() takes an input value, saturation values,
 * 	an array of biquad structures, and the number of biquads in the array.
 * xin is the measured analog voltage from the C connector
 * A difference equation is computed for each biquad, with the outputs
 * 	of the previous biquad passing to the next, and so on.
 * A calculated y0 is returned from the function.
 */
	struct biquad *f = fa; 	// f, pointer to biquad struct
	double y0 = xin; 		// initial input voltage

	// loop through ns biquads
	int i;
	for (i=0; i < ns; i++){
		// assign previous output to current input
		f->x0 = y0;

		// difference equation
		y0 = ((f->b0 * f->x0)+(f->b1 * f->x1)+(f->b2 * f->x2)-(f->a1 * f->y1)-(f->a2 * f->y2)) / (f->a0);

		// update previous values of x and y
		f->x2 = f->x1; f->x1 = f->x0;
		f->y2 = f->y1; f->y1 = y0;

		// increment pointer to next biquad struct in array
		f++;
	}

	y0 = SATURATE(y0, ymin, ymax); 	// saturate final y0
	return y0;						// return final output value
}


