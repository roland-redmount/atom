
#include "library/MachineService.h"
#include "library/math.h"


/**
 * The operator (+ x<INT + y<INT = z>INT)
 */
static bool add1(Atom arguments[], void * state, bool isFirstCall)
{
	arguments[2]._int = arguments[0]._int + arguments[1]._int;
	return true;
}


/**
 * The operator (+ x<INT + y>INT = z<INT)
 * This implements subtraction by solving the equation
 * z = x + y  <->  y = z - x
 */
static bool add2(Atom arguments[], void * state, bool isFirstCall)
{
	arguments[1]._int = arguments[2]._int - arguments[0]._int;
	return true;
}


/**
 * The operator (* x<INT * y<INT = z>INT)
 */
static bool mul1(Atom arguments[], void * state, bool isFirstCall)
{
	arguments[2]._int = arguments[0]._int * arguments[1]._int;
	return true;
}


/**
 * A "co-routine" machine function, returning multiple values.
 * This implements a range iterator (lower @1<INT number @2>INT upper @3<INT)
 * which returns all values @2 between the lower and upper bound, inclusive.
 * The state holds the value returned by the previous call.
 *
 * Successive tuples differ only in @2, which ascends, so the tuples are ordered
 * as RegisterMachineService() requires.
 */
typedef struct {
	Atom number;
} RangeState;


static bool range(Atom arguments[], void * state, bool isFirstCall)
{
	RangeState * rangeState = state;
	if(isFirstCall)
		// begin iterating at the lower bound
		rangeState->number = arguments[0];
	else
		rangeState->number._int++;

	if(rangeState->number._int > arguments[2]._int)
		return false;
	arguments[1] = rangeState->number;
	return true;
}


void MathSetup(void)
{
	RegisterMachineService("+ @1<INT + @2<INT = @3>INT", &add1, 0);
	RegisterMachineService("+ @1<INT + @2>INT = @3<INT", &add2, 0);

	RegisterMachineService("* @1<INT * @2<INT = @3>INT", &mul1, 0);

	RegisterMachineService(
		"lower @1<INT number @2>INT upper @3<INT", &range, sizeof(RangeState));
}
