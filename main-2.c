/*
 * Lab 2
 * Author: trenton
 * Date: 01/31/2025
 *
 * Description: This lab demonstrates my own getchar_keypad() function.
 * The main code block calls fgets_keypad() twice and prints each value to the LCD and console for validation.
 *
 * getchar_keypad() acquires a single character from the keypad at a time.
 * It is added to a buffer, which is displayed on the the LCD.
 * When ENTR is pressed, the buffer is cleared one character at a time by fgets_keypad().
 *
 * The delete key works as expected on the buffer.
 * DEL removes the previous character from the buffer and lcd.
 * when the buffer is empty, del does nothing.
 * When the buffer is full, del works.
 * Once ENTR is pressed, the string is stored in memory.
 *
 * Function Hierarchy:
 *
 * fgets_keypad()		//acquire string of chars
 * 	|-getchar_keypad()	//one char. from input
 * 		|-putchar_lcd()	//one char. to input
 * 		|-getkey()		//get 1 char from keypad
 */

/* includes */
#include <stdio.h>
#include "MyRio.h"
#include "T1.h"

/* prototypes */
int printf_lcd(const char *format,...); 			// prints to LCD
char * fgets_keypad(char *buffer, int bufferlen); 	// takes input from keypad
char getkey(void);									// returns char of keypad button
int putchar_lcd(int c);								// writes char to lcd screen

int getchar_keypad(void); // returns a single character from the keypad as an int

/* Definitions */
#define buf_len 22		// Length of the buffer (final char is EOF)

// main program loop ###################################################################
int main(int argc, char **argv){
/*
 * main() tests the function getchar_keypad() by calling fgets_keypad() twice,
 * then prints the strings to console and lcd via printf_lcd().
 * main() also includes two user prompts and output display formatting.
 */
	// MyRio connection code - required by hardware-------------------------------------
	NiFpga_Status status;							// declare status type
    status = MyRio_Open();		    				// open FPGA session
    if (MyRio_IsNotSuccess(status)) return status;	// test if session opened

    // Lab 2 Test Code		 -----------------------------------------------------------

    char input1[buf_len];
    char input2[buf_len];

    // Test getchar_keypad()
    // call fgets_keypad() function twice
    putchar_lcd('\f');				// clear display
    putchar_lcd('\v');				// move to first line of display
    printf_lcd("Enter input 1\n");	// user prompt for input
    fgets_keypad(input1,buf_len);		// call fgets_keypad()

    putchar_lcd('\f');
    putchar_lcd('\v');
    printf_lcd("Enter input 2\n");
    fgets_keypad(input2,buf_len);

	// print values from fgets_keypad() function calls to console
	printf("Input 1: %s\n", input1);
	printf("Input 2: %s\n", input2);

	// print values from fgets_keypad() function calls to LCD
	putchar_lcd('\f');
	putchar_lcd('\v');
	printf_lcd("Input 1: %s\n", input1);
	printf_lcd("Input 2: %s\n", input2);

    //MyRio ending code - required by hardware -----------------------------------------
    status = MyRio_Close();						// close FPGA session
    return status;								// return status of session
}

// Functions ###########################################################################
int getchar_keypad(void){
/*
 *  getchar_keypad() function
 *  input parameter: void
 *  This function has a buffer. It takes single keypad presses,
 *  	stores them to the buffer, prints them to the lcd,
 *  	and returns the buffer string when ENTR is pressed.
 *  Delete functionality: while there are characters in the buffer,
 *  	delete will remove the most recent char and step the input back.
 *  When the buffer is full: new inputs are not allowed, but delete can be used.
 *  The final character in the buffer will always be an EOF (end of file) character.
 */

	//Declare variables
	static int n = 0;				// Counter variable for chars in buffer
	static char buffer[buf_len];	// We'll be calling our buffer: "buffer"
	static char *pointer;			// Pointer to our buffer: "pointer"
	static char c;					// character value assigned by getkey()

	// if empty buffer condition
	if (n == 0){
		pointer = buffer;	// set pointer to start of buffer
		// while keypad button isn't ENTR
		while ((c=getkey())!= ENT){
			/* Delete Functionality
			 * check if pressed key is delete and that n != 0.
			 * we check that n !=0 so that del does nothing to lcd when empty,
			 * otherwise the cursor would move left one space.
			*/
			if ((c == DEL) && n!=0){
				pointer--;			// decrement pointer to previous value
				*pointer = '\0';	// clear value by assigning NULL
				putchar_lcd('\b');	// move cursor left,
				putchar_lcd(' ');	//  to clear previous character on lcd
				putchar_lcd('\b');	// then reset position.
				n--;
			}
			// for all characters that aren't delete:
			// check buf_len to ensure we don't exceed buffer size.
			/* TODO fix bug: second to last buffer character can't be utilized.
			 * There's one character of buffer overflow happening between calls and I can't find
			 * the exact cause of it. With just buf_len-1 second input is skipped.*/
			else if ((c != DEL)&&(n < (buf_len-2))){
				*pointer++ = c;	// put c in buffer at location pointed to by pointer, increment pointer
				n++; 			// increment n
				putchar_lcd(c); // print c to LCD
			}
		}
		n++;				// increment counter
		pointer = buffer; 	// set pointer to start of buffer
	}

	// if more than one character in buffer, after input, this returns buffer
	if (n > 1){
		n--;				// decrement n
		return *pointer++;	//return character (in the form of an int) pointed to and increment pointer
	}
	// else (final character in buffer), send EOF
	else{
		n--;		// decrement n
		return -1; 	// -1 is EOF (end of file) character
	}
}



