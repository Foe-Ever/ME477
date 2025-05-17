/* Lab #0 - <trenton> */

/* includes */
#include "stdio.h"
#include "MyRio.h"
#include "T1.h"

/* prototypes */
int sumsq(int x); /* sum of squares */

/* definitions */
#define N 4 /* number of loops */

int main(int argc, char **argv) {
	NiFpga_Status status;
	static int x[10]; /* total */
	static int i; /* index */
	status = MyRio_Open(); /* Open NiFpga.*/
	if (MyRio_IsNotSuccess(status)) return status;

	printf_lcd("\fHello, Trenton\n\n");
	for (i=0; i<N; i++) {
		x[i] = sumsq(i);
		printf_lcd("%d, ",x[i]);
	}
	status = MyRio_Close(); /* Close NiFpga. */
	return status;
}
int sumsq(int x) {
	static int y=4;

	y = y + x*x;
	return y;
}

/* Results:
 * The results are consistent with my understanding of the program.
 * sumsq adds i^2 to the value of y, with y updating each loop to the current total.
 * This starts at y=4 and i=0, so 4 is the first value.
 * i increments by 1 each loop.
 * At the second pass, 1*1 is added to 4 = 5.
 * Then i increments again and i=2, 2*2 is added to 5 = 9.
 * These values are stored in the array x[] starting at x[0].
 *  */

