/*
 * Lab 3
 * Author: Trenton
 * Date: 02/07/25
 *
 * This program has 3 main components:
 * putchar_lcd(), getkey(), and the main code block which calls these functions.
 * putchar_lcd() places a single ASCII character on the LCD.
 * It also accepts escape sequences which control the display and cursor.
 * getkey() utilizes digital IO pins on the MyRio to read which keypad button
 *   has been pressed and depressed then returns that key's value to the program.
 * These are low-level driver functions which are used
 *   when getting keypad input or printing to the LCD.
 *
 */

/* includes */
#include <stdio.h>
#include "MyRio.h"
#include "T1.h"
#include "UART.h"	// For UART communication with MyRIO
#include "DIO.h"	// For digital input output pin use

/* prototypes */
int putchar_lcd(int c);	//takes character input, prints to lcd
char getkey(void);		//identifies keypad key depressed
void wait2(void);		// WAIT

NiFpga_Bool Dio_ReadBit(MyRio_Dio *channel);	// reads dio bit, sets channel to high-z
void Dio_WriteBit(MyRio_Dio *channel, NiFpga_Bool value);	// digital output write


/* definitions */
#define buf_len 20		// Length of the buffer

// main program loop ###################################################################
int main(int argc, char **argv){
	/*
	 * main()tests putchar_lcd() and getkey()
	 *
	 * putchar_lcd() is called once with a valid coded input,
	 * then again with an invalid input (>255). Results are also printed to console.
	 *
	 * getkey() is called and progresses the program when the user presses a key.
	 *
	 * fgets_keypad() collects a string which is displayed via printf_lcd()
	 * printf_lcd() escape sequences are called and displayed to show functionality of;
	 * \b,\f,\n,\v
	 *
	 */

	// MyRio connection code - required by hardware-------------------------------------
	NiFpga_Status status;							// declare status type
	status = MyRio_Open();		    				// open FPGA session
	if (MyRio_IsNotSuccess(status)) return status;	// test if session opened

	// Lab 3 Test Code		 -----------------------------------------------------------

	//initialize input variables
	char test2[buf_len];

	// call putchar_lcd() with valid and invalid inputs
	putchar_lcd('\f');	// clear display
	putchar_lcd('\v');	// move to first line of display
	putchar_lcd(48);	// valid input printed to lcd
	putchar_lcd(260);	// invalid input "printed" to lcd

	// display results to console
	putchar(48);		// valid input printed to console
	printf("\nValid Input ");
	putchar(260);		// invalid input "printed" to console
	printf("\nInvalid Input Not Displayed\n");

	//call getkey(), display to lcd and console
	putchar_lcd('\n');		// move to next line of display
	printf_lcd("Press a button\n");
	char test1 = getkey();	// call getkey() individually once
	putchar_lcd('\f');
	printf("Button pressed: %c\n",test1);
	printf_lcd("Button pressed: %c\n",test1);

	//collect a string with fgets_keypad()
	printf_lcd("Enter string 1\n");			// prompt user for string input
	putchar_lcd('\b');						// test '\b'
	fgets_keypad(test2,buf_len);			// collect buffered string
	printf_lcd("\nString: %s", test2);		// display string to lcd

	//MyRio ending code - required by hardware -----------------------------------------
	status = MyRio_Close();						// close FPGA session
	return status;								// return status of session
}

int putchar_lcd(int c){
/*
 * putchar_lcd places a single character on the LCD.
 * Any ASCII code or these escape sequences: ('\f','\v','\n','\b')
 * Function input 'c' is in the range 0 to 255.
 * EOF is returned to indicate an error if c is outside of that range.
 *
 * Function Pseudo Code:
 * -Initialize B's UART port the first time putchar_lcd() is called.
 * -If input character is in range of [0,255],
 * 		-send to display or send escape sequence directive
 * -check for success of UART write
 * -if write is successful,
 * 		-return character to calling program
 * 		-else: return EOF
 *
 */

	// Function Variables
	static int i = 0;	// default i value, indicates UART hasn't been opened
	uint8_t d[2];		// data array sized to accommodate escape sequences
	int n = 1;			// assume length of character array is 1

	// UART Variables
	static MyRio_Uart uart;	// port information structure
	int baud_rate = 19200;	// T1 target display baud rate
	NiFpga_Status status;	// returned value

	// Check for first function call, if so initialize UART
	if (i==0){
		uart.name = "ASRL2::INSTR";				// UART on connector B
		uart.defaultRM = 0;						// define resource manager
		uart.session = 0;						// session reference number
		status = Uart_Open(&uart,				// port information
							baud_rate,			// baud rate, bits per second
							8,					// number of data bits
							Uart_StopBits1_0,	// 1 stop bit
							Uart_ParityNone);	// no parity bit
		// check for success of connection, return error flag if unsuccessful
		if (status < VI_SUCCESS){
			return EOF;
		}
		i = 1;	// change index to show uart connection has been established
	}
	// Check for escape sequences, convert them to decimal equivalents via cast
	if ((c == '\b') || (c == '\f') || (c == '\n') || (c == '\v')){
		d[0] = (uint8_t)c;		// cast c to ASCII decimal

		/* this stupid system uses '\n' to mean '\n''\r'
		// '\r' is carriage return and moves cursor to beginning of current line
		// so this functionality has to be hard coded in.
		// ascii \n = 10, \r = 13
		// This stupid hardware also thinks \r is \n\r.
		*/
		if (c == '\n'){
			d[0] = 13;		// sighs
		}
		// '\f' requires a second code for our purposes
		if (c == '\f'){
			d[1] = 17;			// set second array value
			n = 2;				// update number of data codes
		}
	}
	// Check if c is outside extended ASCII range
	else if (c>255 || c<0){
		return EOF;				// throw error flag
	}
	// c is regular ASCII, set write array to its decimal value
	else{
		d[0] = (uint8_t)c;
	}
	//Uart_Write takes 3 inputs: &uart, d, n
	// and writes characters to lcd
	status = Uart_Write(&uart,	// port information
						d,		// data array
						n);		// number of data codes
	// check for unsuccessful write
	if (status < VI_SUCCESS){
		return EOF;				// throw error flag
	}
	// successful write!
	else{
		return c;
	}
}

char getkey(void){
	/*
	 * getkey() identifies which keypad button is depressed and returns its value.
	 * This is accomplished by reading digital input voltage to the MyRio DIO pins 0-7.
	 * A single keypad column is driven to low voltage, its rows scanned.
	 * If a row is low, the key connecting to that row and column combination must be depressed.
	 * This is repeated for each column as all keys are scanned.
	 *
	 * Function Pseudo Code
	 * -initialize DIO channels
	 * -b set true
	 * -for each column c
	 * 		-for each column C
	 * 			-read C to set high-Z input
	 * 		-write low to c
	 * 		-for each row r
	 * 			-read r to b
	 * 			-if b is false		//check for depressed key
	 * 				-break row loop
	 * 		-if b is false
	 * 			-break column loop
	 * 		-else
	 * 			wait ## ms			// delay rescan
	 * -wait for key to be depressed
	 * 		-wait ## ms				// prevent unintended duplicates
	 * -lookup key in table
	 * -return value
	 *
	 */

	// function variables
	NiFpga_Bool b = NiFpga_True;	// set b to true, flagged false when key depressed

	int c;		// column identifier
	int Col;	// also column identifier
	int r;		// row identifier

	// Key Code Lookup Table
	const char table[4][4] ={
			{'1', '2', '3', UP},
			{'4', '5', '6', DN},
			{'7', '8', '9', ENT},
			{'0', '.', '-', DEL}
	};

	// DIO Channel Initialization
	MyRio_Dio ch[8];				// array of channels
	int i;
	for (i = 0; i < 8; i++){	// in DIOB_70 register bank
		ch[i].dir = DIOB_70DIR;		// line direction (output/input) bit
		ch[i].out = DIOB_70OUT;		// output bit
		ch[i].in = DIOB_70IN;		// input bit
		ch[i].bit = i;				// bit index
	}
	// Run this loop until a key is depressed
	while (b == NiFpga_True){
		for (c = 0; c<4; c++){
			//cycles through each column
			for (Col = 0; Col<4; Col++){
				NiFpga_Bool bit = Dio_ReadBit(&ch[Col]);	// sets all columns to high-z
			Dio_WriteBit(&ch[c], NiFpga_False);				// sets current column to low-z
			}
			// cycles through each row
			for (r = 4; r<8; r++){
				b = Dio_ReadBit(&ch[r]);	// reads if current block is set high or low
				// identifies that a key has been depressed, leaves row loop
				if (b == NiFpga_False){
					break;
				}
			}
			// identifies that a key has been depressed, leaves column loop
			if (b == NiFpga_False){
				break;
			}
			// wait for everything to stabilize before repeating/duplicating
			else{
				wait2();
			}
		}
	}
	// waits to return character until it is depressed
	// doesn't send pressed down character
	while (Dio_ReadBit(&ch[r])==NiFpga_False){
		wait2();
	}
	char k = table[r-4][c];	//uses lookup table to return keypad value
	return k;

}

void wait2(void){
	/*
	 * This function waits for the prescribed amount of milliseconds.
	 * Literally wastes time and electricity counting down.
	 * From textbook
	 */
	uint32_t i;
	i = 417000;
	while (i>0){
		i--;
	}
	return;
}



