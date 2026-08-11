
#include "library/MachineService.h"
#include "library/math.h"


/**
 * The operator (+ x<INT + y<INT = z>INT)
 */
static bool add1(Atom arguments[])
{
	arguments[2]._int = arguments[0]._int + arguments[1]._int;
	return true;
}


/**
 * The operator (+ x<INT + y>INT = z<INT)
 * This implements subtraction by solving the equation
 * z = x + y  <->  y = z - x
 */
static bool add2(Atom arguments[])
{
	arguments[1]._int = arguments[2]._int - arguments[0]._int;
	return true;
}


/**
 * The operator (* x<INT * y<INT = z>INT)
 */
static bool mul1(Atom arguments[])
{
	arguments[2]._int = arguments[0]._int * arguments[1]._int;
	return true;
}


void MathSetup(void)
{
	// The two services of the (+ + =) relation, which the first call creates
	RegisterMachineService("+ @1<INT + @2<INT = @3>INT", &add1);
	RegisterMachineService("+ @1<INT + @2>INT = @3<INT", &add2);

	RegisterMachineService("* @1<INT * @2<INT = @3>INT", &mul1);
}
