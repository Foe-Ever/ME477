/*
 * Lab 7
 * Author: Trenton
 * Date: 03/14/25
 * Description: The purpose of this code is to implement PI control of
 * a DC motor. This is implemented by a timer based interrupt system,
 * ctable2() which allows a user to edit the gains of the system, and
 * biquad cascade for the PI calculations. vel() provides rpm calculations
 * to the readout. Program configurations can be changed while the program
 * is running thanks to ctable2() running on a separate thread. The table
 * is shared between threads. 250 data points for each reference velocity
 * are saved to a .mat file for analysis.
 */

/* includes */
#include <stdio.h>
#include "MyRio.h"
#include "T1.h"

#include "AIO.h"		// analog input and output for MyRio
#include <pthread.h>	// linux multithreading
#include "TimerIRQ.h"	// timer irq for interrupt method
#include "matlabfiles.h"// matlab file creation
#include "ctable2.h"	// ctable2 for editing values
#include "Encoder.h"	// quadrature encoder

//#include "emulate.h"	// motor emulation

// saturation macro for cascade(), provided by book
#define SATURATE(x,lo,hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define IMAX 250 //matlab data points
#define M_PI 3.14159265358979323846 // PI DAY

/* biquad structure for cascade()*/
struct biquad {
  double b0; double b1; double b2;   // numerator
  double a0; double a1; double a2;   // denominator
  double x0; double x1; double x2;   // input
  double y1; double y2;              // output
};

/* prototypes */ //--------------------------------------------------------

double vel(void);	// velocity calculation

/* ctable2
int ctable2(char *title,
            struct table *entries,
            int nval);*/

// ISR and interrupt scheduler
void* Timer_ISR(void *thread_resource);

// biquad cascade implementation
double cascade(double xin,         // input
               struct biquad *fa,  // biquad array
               int    ns,          // no. segments
               double ymin,        // min output
               double ymax);       // max output

//encoder prototypes
NiFpga_Status EncoderC_initialize(NiFpga_Session myrio_session,
		MyRio_Encoder *channel);	// Encoder initialize
uint32_t Encoder_Counter(MyRio_Encoder *channel); // Encoder count retrieval

/* definitions */ //--------------------------------------

// Encoder global
MyRio_Encoder encC0; // channel encC0

//Globally defined thread resource structure
typedef struct {
  NiFpga_IrqContext irqContext;  // context
  table *a_table;                // table
  NiFpga_Bool irqThreadRdy;      // ready flag
} ThreadResource;

/*//ctable2 structure in ctable2.h
typedef struct {
  char *e_label;  // entry label label
  int e_type;     // entry type (0-show; 1-edit)
  double value;   // value
} table; */

NiFpga_Session myrio_session;// myrio session macro required for book code template

// main program loop #############################################################
int main(int argc, char **argv){
/* Description of main()
 * Initialize myrio, table editor variables, and timer thread.
 * Calls ctable2. When "<-" is pressed, ctable2 returns and main continues.
 * Cleans up threads and ends myrio session.
 *
 * ctable is a shared table between the timer ISR and ctable2().
 * It can be updated while the ISR is running and its changes are reflected
 *  in the next iteration of the ISR. It's used to control proportional and
 *  integral gain constants, as well as reference velocity and BTI.
 */
	// 1) myRIO session open - required by hardware-------------------------------
	NiFpga_Status status;							// declare status type
	status = MyRio_Open();		    				// open FPGA session
	if (MyRio_IsNotSuccess(status)) return status;	// test if session opened

	// initialize table editor variables
	char *Table_Title = "Velocity Control";
	table my_table[] = {
	  {"V_R: rpm  ", 1, 0.0}, 	// 0 show, 1 edit (first value)
	  {"V_J: rpm  ", 0, 0.0},
	  {"VDAout: mV ", 0, 0.0},
	  {"Kp: V-s/r1 ", 1, 0.104},// value provided by book
	  {"Ki: V/r1 ", 1, 2.07},	// value provied by book
	  {"BTI: ms  ", 1, 5}		// 5 ms
	};
	int nval = 6; // number of table parameters

	// configure timer interrupt and create timer thread ----------------------
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
	// point to table
	irqThread0.a_table = my_table;
	// set indicator to allow new thread
	irqThread0.irqThreadRdy = NiFpga_True;
	// create thread calling Timer_ISR()
	irq_status = pthread_create(&thread, NULL, Timer_ISR, &irqThread0);

	// call ctable2
	ctable2(Table_Title, my_table, nval); // returns 0 when "<-" pressed

	// ) Terminate ISR and unregister interrupt -----------------------------
	irqThread0.irqThreadRdy = NiFpga_False;		// set flag to false, signals thread end
	irq_status = pthread_join(thread, NULL);	// join threads
	irq_status = Irq_UnregisterTimerIrq(&irqTimer0, irqThread0.irqContext);

	// ) MyRio session close - required by hardware ---------------------------
	status = MyRio_Close();						// close FPGA session
	return status;								// return status of session
}// end of main()

// Functions ###################################################################

void* Timer_ISR(void *thread_resource){
/* Description of Timer_ISR
 * This function schedules timed interrupts and initializes the motor encoder
 * and AIO, calls vel() for velocity and implements the PI control law by
 * computing current error between reference and actual speed, then calls
 * cascade() to compute the control value. This value is sent to the motor
 * and the table is updated with all appropriate values.
 *
 */

	// 1) Initialize Everything: cast input resource
	ThreadResource *threadResource = (ThreadResource*) thread_resource;

	// computation variables
	double V_out;
	double speed_error;

	// variable names for table entries
	double *Omega_R = &((threadResource->a_table + 0)-> value);
	double *Omega_J = &((threadResource->a_table + 1)-> value);
	double *VDA_out = &((threadResource->a_table + 2)-> value);
	double *Kp = &((threadResource->a_table + 3)-> value);
	double *Ki = &((threadResource->a_table + 4)-> value);
	double *BTI = &((threadResource->a_table + 5)-> value);

	// set cascade() parameters
	double v_min = -10;	// minimum saturation voltage (v)
	double v_max = 10;	// maximum saturation voltage (v)
	int myFilter_ns = 1;			// # of biquad sections

	static struct biquad myFilter[] = {
		  {0.0000e+00,  0.0000e+00, 0.0000e+00,
		   1.0000e+00, -1.0000e+00, 0.0000e+00, 0, 0, 0, 0, 0},
	};

	// Initialize Encoder interface
	EncoderC_initialize(myrio_session, &encC0);

	// initialize analog output, connector C, to drive motor
	MyRio_Aio AOC0;		// C, analog output 1
	Aio_InitCO1(&AOC0);	// initialize o1

	Aio_Write(&AOC0, 0);// start at 0V output
	// Note: voltage is maintained until updated with another Aio_Write()

	// MATLAB VARIABLES
	int error_mat;
	static double Omega_J_buf[IMAX];
	static double VDA_out_buf[IMAX];
	static double RPM_prev_mat;
	static double RPM_curr_mat;
	static double Kp_mat;
	static double Ki_mat;
	static double BTI_mat;
	double Omega_init = 0;	// initial Omega_R value
	static double *bp_oj = Omega_J_buf;
	static double *bp_vda = VDA_out_buf;

	// 2) while loop to process interrupts, checks irqThreadRdy -----------------
	while (threadResource->irqThreadRdy == NiFpga_True){
	/* timer loop
	 * 2.1) schedule interrupt
	 * 2.2) vel()
	 * 2.3) compute ai, bi from Kp, Ki, update biquad
	 * 2.4) omega_ref-omega_actual
	 * 2.5) cascade() with 10v saturation
	 * 2.6) AOC0 output
	 * 2.7) update table
	 * 2.8) save results to matlab
	 * 2.9) acknowledge interrupt
	 */
		//wait for interrupt
		uint32_t irqAssert = 0;
		Irq_Wait(threadResource->irqContext,
				TIMERIRQNO,
				&irqAssert,
				(NiFpga_Bool*)&(threadResource->irqThreadRdy));
		// check for timer IRQ assert
		if (irqAssert & (1<<TIMERIRQNO)){
			// 2.1) Schedule next interrupt
			//NiFpga_WriteU32(myrio_session, IRQTIMERWRITE, timeoutValue);
			NiFpga_WriteU32(myrio_session, IRQTIMERWRITE, (*BTI*1000));
			//Note: bti from table controls wait time between timed interrupts
			NiFpga_WriteBool(myrio_session, IRQTIMERSETTIME, NiFpga_True);

			//ISR service code --------------------------------------------------
			// 2.2) call vel
			*Omega_J = (vel() * 60)/(2048.0 * (*BTI/1000)); // rpm

			// 2.3) compute ai, bi from Kp, Ki and update biquad
			myFilter->b0 = *Kp + (*Ki*(*BTI/1000)/2);
			myFilter->b1 = -*Kp + (*Ki*(*BTI/1000)/2);

			// 2.4) computer current error
			speed_error = (*Omega_R - *Omega_J)*2*M_PI/60;	// rad/s

			// 2.5) call cascade() to calculate y(n), aka v_out
			V_out = cascade(speed_error, myFilter, myFilter_ns, v_min, v_max); // volts

			// 2.6) send control voltage value to DAC
			Aio_Write(&AOC0, V_out);

			// 2.7) update table values
			*VDA_out = V_out * 1000;	// V to mV

			// 2.8) MATLAB data
				// save speed and output voltage to buffer if not full
			if (bp_oj < (Omega_J_buf+IMAX)){
				*bp_oj++ = *Omega_J;
				*bp_vda++ = V_out;
			}
				// handle a change in reference velocity
			if (Omega_init != *Omega_R){
				bp_oj = Omega_J_buf;		// reset index
				bp_vda = VDA_out_buf;		// reset index
				RPM_prev_mat = Omega_init;	// previous ref vel
				RPM_curr_mat= *Omega_R;		// current ref vel
				Omega_init = *Omega_R;		// update initial ref vel
				BTI_mat = *BTI/1000;		// bti (ms)
				Kp_mat = *Kp;				// Kp
				Ki_mat = *Ki;				// Ki
			}

			// 2.9) acknowledge interrupt
			Irq_Acknowledge(irqAssert);
		}
	}
	// create and save matlab data
	error_mat=101;			// Error code
	MATFILE *mf;
	mf = openmatfile("Lab7_trenton.mat", &error_mat);
	if(!mf) printf("Can't open mat file %d\n", error_mat);
	matfile_addstring(mf, "myName", "Trenton Fletcher");
	matfile_addmatrix(mf, "Omega_J", Omega_J_buf, IMAX, 1, 0);
	matfile_addmatrix(mf, "VDA_Out", VDA_out_buf, IMAX, 1, 0);
	matfile_addmatrix(mf, "Previous_rpm", &RPM_prev_mat, 1, 1, 0);
	matfile_addmatrix(mf, "Current_rpm", &RPM_curr_mat, 1, 1, 0);
	matfile_addmatrix(mf, "BTI", &BTI_mat, 1, 1, 0);
	matfile_addmatrix(mf, "Kp", &Kp_mat, 1, 1, 0);
	matfile_addmatrix(mf, "Ki", &Ki_mat, 1, 1, 0);
	matfile_close(mf);

	// terminate thread
	Aio_Write(&AOC0, 0);// for safety, set output voltage to 0 volts
	pthread_exit(NULL); // exit thread
	return NULL;
}

// Functions from previous labs ########################################
double vel(void){
/* measures velocity (BDI/BTI) by reading current encoder count,
 * taking the difference between the previous count,
 * returns the double speed (BDI/BTI)
 */
	// function variables
	static int i_vel=0; //flag to check if first vel call
	static uint32_t Cn; // current encoder counter
	static uint32_t Cn1; // previous encoder count
	double speed; // speed, units of BDI/BTI

	Cn = Encoder_Counter(&encC0); // get encoder count
	// Check if first call to vel
	if (i_vel==0){
		Cn1 = Cn; // only on first call set Cn1 to Cn
		i_vel = 1; // set flag that vel has been called
	}
	speed = Cn - Cn1; //calculate speed BDI/BTI
	Cn1 = Cn; // update previous count
	return speed;
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

