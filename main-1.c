/*
 * Lab 1
 * Author: trenton
 * Date: 01/21/25
 * Description: implement basic keypad functionality for user input, provide input validation, handle errors
 * Driver creation for double_in() and printf_lcd() functions.
 */

/* includes */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "MyRio.h"
#include "T1.h"

/* prototypes */
double double_in(char *prompt);	//validates and stores user number input
int printf_lcd(const char *format,...); //prints to LCD

char * fgets_keypad(char *buffer, int bufferlen); // takes input from keypad
int sscanf(const char *s, const char *format, ...); //converts one type to another

char* strpbrk(const char *s1, const char *s2); //first occurrence of a character
char* strchr(const char *s, int c); //first occurrence of c in s
char* strrchr(const char *s, int c); //last occurrence of c in s

// main program loop ###################################################################
int main(int argc, char **argv){
/*
 * main() tests the functions double_in() and printf_lcd() by running double_in() twice
 * and then printing to console and then lcd via printt_lcd().
 */
	//MyRio connection code - required by hardware--------------------------------------
	NiFpga_Status status;						// declare status type
    status = MyRio_Open();		    			// open FPGA session
    if (MyRio_IsNotSuccess(status)) return status;	// test if session opened

    // Lab 1 Test Code		 -----------------------------------------------------------

    // Test doubl_in()
    // call double_in() function twice
    double test1 = double_in("Enter test 1: ");
    double test2 = double_in("Enter test 2: ");
    // print values from double_in() function calls to console
    printf("test1=%lf\n", test1);
    printf("test2=%lf\n", test2);
    // print values from double_in() function calls to LCD
    putchar_lcd('\f');	// clear display
    putchar_lcd('\v');	// move to first line of display
    printf_lcd("test1=%lf\n", test1);
    printf_lcd("test2=%lf\n", test2);

    //MyRio ending code - required by hardware -----------------------------------------
	status = MyRio_Close();						// close FPGA session
	return status;								// return status of session
}

// Functions ###########################################################################

double double_in(char *prompt){
	/*
	 *double_in() function
	 *input parameter: prompt string pointer
	 *takes user keypad input, terminated by entr, performs error checking, returns value as float
	 *errors checked for: empty value, up or down key, double radix, negative not in first position (including double use)
	 *TODO errors to be checked: "-." returns null if first value, returns previous value if second input.
	 */

	//declare variables
	int error = 1;			// initialize/set error flag
	char buffer [40];	// declare string user input will be stored in
	double value;			// initialize value, what will be returned by the function

	// clear display
	putchar_lcd('\f');

	//function loop
	while (error == 1){
		//putchar_lcd('\f');	// clear display
		putchar_lcd('\v');	// move to first line of display
		printf_lcd(prompt);	// print given function prompt
		buffer[0] = '\0';	// empties buffer each while loop iteration (clears bad input data)

		// take user input
		fgets_keypad(buffer,40);	// puts input characters into string, terminated by enter

		// test user input, return error messages if bad input, allow user to re-enter input
		// test if string is empty
		if (buffer[0] == '\0'){
			putchar_lcd('\f');				// clear lcd
			printf_lcd("\n");				// go to second line
			printf_lcd("Short. Try Again");	// display error message
			continue;						// go back to top of while loop so user can re-enter value

		// test if string has extra "-" || up or down key pressed || two '.' in string
		}else if((strpbrk(buffer,"[]")!= NULL) || (strpbrk(buffer+1,"-")!= NULL) || (strchr(buffer,'.') != NULL && strchr(buffer,'.') != strrchr(buffer,'.'))){
			putchar_lcd('\f');
			printf_lcd("\n");
			printf_lcd("Bad Key. Try Again");
			continue;
		}else{
			// input was validated as a number.
			error = 0;	// set error to 0 to end loop
			//TODO throw error for "-."
		}
	}

	// convert ascii string to double (long float)
	sscanf(buffer, "%lf", &value);
	// return float val
	return value;
}

int printf_lcd(const char *format,...){
	/*
	 * printf_lcd() function
	 * input: format string with variable number of arguments
	 * action: prints string to LCD via putchar_lcd
	 * output: number of characters in string, or negative value for error
	 */

	int n; //string length counter
	char string[80]; //buffer
	va_list args;
	va_start(args, format);							// start parse
		n = vsnprintf(string, 80, format, args); 	// parse to C string
	va_end(args);									// end parse

	//detect conversion error
	if (n<=0){
		return -1;	// return -1 to signify error in parsing
	}

	char *p = string;	// initialize pointer to string
	while (*p) putchar_lcd(*p++);	// iterate through string

	return n;	// return n, number of characters in string
}




