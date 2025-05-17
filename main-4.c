/*
 * Lab 4
 * Author: Trenton
 * Date: 02/21/25
 * Description:
	Implement a finite state machine to run and monitor the rpm of a DC motor.
	Provide PWM signal to current amplifier to run motor.
	When printS is pressed, display the calculated rpm to the LCD.
	When stopS is pressed, current is no longer supplied to the motor.
	stopS also saves the rpm data to a matlab file.
 */

/* includes --------------------------------------------*/
#include <stdio.h>
#include "Encoder.h"
#include "MyRio.h"
#include "DIO.h"
#include "T1.h"
#include "matlabfiles.h"
#include "UART.h"
//#include "emulate.h" // used for motor emulation, has limitations

/* prototypes ------------------------------------------*/
double double_in(char *string);
void wait(void);
void initializeSM(void);
void initializeHardware(void);
double vel(void);
NiFpga_Status EncoderC_initialize(NiFpga_Session myrio_session,
		MyRio_Encoder *channel);	// Encoder initialize
uint32_t Encoder_Counter(MyRio_Encoder *channel); // Encoder count retrieval
NiFpga_Bool Dio_ReadBit(MyRio_Dio *channel); // Read pins to detect switch presses
void Dio_WriteBit(MyRio_Dio *channel, NiFpga_Bool value); // Digital output write


/* definitions -----------------------------------------*/
typedef enum {
	STATE_LOW = 0,
	STATE_HIGH,
	STATE_SPEED,
	STATE_STOP,
	STATE_EXIT,
	NUM_STATES // used to set function array size
} State_Type; // enumeration of states
static State_Type curr_state; // current state
static int clock_count;
/* State Functions Array of Pointers*/
static void (*state_table[NUM_STATES])(void);
// Encoder global variables
NiFpga_Session myrio_session;
MyRio_Encoder encC0; // channel encC0
static int N; // number of wait periods
static int M; // number of on periods
// DIO
MyRio_Dio run;
MyRio_Dio printS;
MyRio_Dio stopS;
//Problem L4.7 Print to MATLAB
#define IMAX 200			//max points
static double buffer[IMAX];	//speed buffer
static double *bp = buffer;	//buffer pointer

/* State Functions ----------------------------------------*/
void stateLOW(void){
/* Resets clock count, raises wave, detects stopS and printS */
	// detect if BTI has ended
	if (clock_count == N){
		clock_count = 0; // reset clock count
		Dio_WriteBit(&run, NiFpga_True); // set run to high

		// check if stopS is pressed
		if (Dio_ReadBit(&stopS) == NiFpga_True){
			curr_state = STATE_STOP;
			Dio_WriteBit(&run, NiFpga_False);
		}
		// check if printS is pressed
		else if (Dio_ReadBit(&printS) == NiFpga_True){
			curr_state = STATE_SPEED;
		}
		// no button presses, goes to high state
		else{
			curr_state = STATE_HIGH;
		}
	}
}

void stateHIGH(void){
/*Once clock reaches M or >M, changes run and curr_state to LOW*/
	if(clock_count >= M){
		Dio_WriteBit(&run, NiFpga_False); // set run to low
		curr_state = STATE_LOW;
	}
}

void stateSPEED(void){
/* calls vel(), calculates RPM, prints RPM to LCD
 * vel = BDI/BTI, BDI/2048 = revolutions
 * BTI * wait_time * N wait periods = seconds
 * seconds / 60 = minutes
 *
 * 	rpm = num / denom
 * 	num = vel/2048
 * 	denom = wait_time * N / 60
 * This function has a long runtime which leads to issues
 */
	double wait_time = 0.005; // (seconds) wait() computed time: 5 ms
	double rpm = (vel() * 60)/(2048.0 * N * wait_time); // rpm
	printf_lcd("\fspeed: %g rpm",rpm); 	// print calculated rpm to LCD
	curr_state = STATE_HIGH; 	// sets current state to HIGH
	// Matlab code 
	if (bp < buffer + IMAX) {
		*bp++ = rpm;
	}
}

void stateSTOP(void){
/* stops supplying power to motor, signals stopping, signals exit
 * and saves response to a MATLAB file.
 */
	Dio_WriteBit(&run, NiFpga_False); // verify run is low
	printf_lcd("\fstopping.");
	curr_state = STATE_EXIT;
	//save matlab file
	int err=101;			// Error code
	double Nc= (double)N;	// cast inputs as doubles
	double Mc= (double)M;
	MATFILE *mf;
	mf = openmatfile("Lab4_trenton.mat", &err);	// open file
	if(!mf) printf("Can't open mat file %d\n", err);
	matfile_addstring(mf, "myName", "Trenton Fletcher");
	matfile_addmatrix(mf, "N", &Nc, 1, 1, 0);
	matfile_addmatrix(mf, "M", &Mc, 1, 1, 0);
	matfile_addmatrix(mf, "vel", buffer, IMAX, 1, 0);
	matfile_close(mf);		// close file
}

/* state functions pointer array */
static void (*state_table[])(void)={
		stateLOW,stateHIGH,stateSPEED, stateSTOP
};

double vel(void){
/* measures velocity (BDI/BTI)
 * by reading current encoder count,
 * taking the difference between the previous count,
 * returns the double speed (BDI/BTI) */
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

void initializeHardware(void){
/* Initialize DIO pins and registers for MyRio */
	// Channel 1, printS button press input
	printS.dir = DIOC_70DIR;   // "70" used for DIO 0-7
	printS.out = DIOC_70OUT;
	printS.in  = DIOC_70IN;
	printS.bit = 1;
	// Channel 3, run output
	run.dir = DIOC_70DIR;
	run.out = DIOC_70OUT;
	run.in  = DIOC_70IN;
	run.bit = 3;
	// Channel 5, stopS button press input
	stopS.dir = DIOC_70DIR;
	stopS.out = DIOC_70OUT;
	stopS.in  = DIOC_70IN;
	stopS.bit = 5;
	// Initialize Encoder interface
	EncoderC_initialize(myrio_session, &encC0);
}

void initializeSM(void){
/* State Machine Initialization Function
 * Sets start conditions for FSM
 * 1) Set run = zero
 * 2) set initial state to low
 * 3) set the clock count to zero
 * DIO channel and encoder initialization are in
 *  initializeHardware() for improved readability.
 * */
	Dio_WriteBit(&run, NiFpga_False);
	curr_state = STATE_LOW;
	clock_count = 0;
}

void wait(void) {
  uint32_t i;
  i = 417000;
  while (i > 0) {
    i--;
  }
  return;
}

int main(int argc, char **argv){
/* Main Program Loop
 * Sets up MyRio connection. Initializes hardware connection
 * and Finite State Machine. Prompts user for N wait intervals
 * and M "on" intervals. Runs the FSM loop which calls to the
 * current state and increases the clock count by 1 each loop.
 * When the state is EXIT, the program closes connection with MyRio*/
	// MyRio connection code - required by hardware-------------------------------------
	NiFpga_Status status;							// declare status type
	status = MyRio_Open();		    				// open FPGA session
	if (MyRio_IsNotSuccess(status)) return status;	// test if session opened

// Lab 4 Main Loop		 -----------------------------------------------------------

	// initialize DIO channels and encoder
	initializeHardware();
	// initialize state machine
	initializeSM();

	// Request user input for N wait intervals and M on intervals
	N = double_in("Wait intervals:");
	M = double_in("On intervals:");
	// if M > N, request new M.
	while (M >= N){
		M = double_in("On intervals:");
	}

	// state machine loop
	// shutdown if state is exit
	while(curr_state != STATE_EXIT){
		state_table[curr_state]();	//call current state function
		wait();						// calibrated wait period
		clock_count++;				// increment clock counter
		}
	//MyRio exit code - required by hardware -----------------------
	status = MyRio_Close();	// close FPGA session
	return status;			// return status of session
}

