
#include "library/MachineService.h"
#include "library/math.h"


/**
 * The operator (+ x<INT + y<INT = z>INT)
 */
static bool add1Call(void * state, Atom arguments[], void * operatorData)
{
	arguments[2]._int = arguments[0]._int + arguments[1]._int;
	return true;
}


/**
 * The operator (+ x<INT + y>INT = z<INT)
 * This implements subtraction by solving the equation
 * z = x + y  <->  y = z - x
 */
static bool add2Call(void * state, Atom arguments[], void * operatorData)
{
	arguments[1]._int = arguments[2]._int - arguments[0]._int;
	return true;
}


/**
 * The operator (* x<INT * y<INT = z>INT)
 */
static bool mul1Call(void * state, Atom arguments[], void * operatorData)
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
 * 
 */
typedef struct {
	Atom number;
} RangeState;

// initialize state to the lower value
static void rangeSetup(void * state, Atom arguments[], void * operatorData)
{
	RangeState * rangeState = state;
	rangeState->number = arguments[0];
}


static bool rangeCall(void * state, Atom arguments[], void * operatorData)
{
	RangeState * rangeState = state;
	if(rangeState->number._int > arguments[2]._int)
		return false;
	arguments[1] = rangeState->number;
	rangeState->number._int++;
	return true;
}


static uint32 providerID;

void MathSetup(void)
{
	providerID = RequestProviderID();

	RegisterMachineService(
		providerID, "+ @1<INT + @2<INT = @3>INT",
		(MachineOperatorSpec) {.call = add1Call}, 0, 0);
		

	RegisterMachineService(
		providerID, "+ @1<INT + @2>INT = @3<INT", 
		(MachineOperatorSpec) {.call = add2Call}, 0, 0);

	RegisterMachineService(
		providerID, "* @1<INT * @2<INT = @3>INT",
		(MachineOperatorSpec) {.call = mul1Call}, 0, 0);

	RegisterMachineService(
		providerID, "lower @1<INT number @2>INT upper @3<INT",
		(MachineOperatorSpec) {.setupState = rangeSetup, .call = rangeCall},
		0,
		sizeof(RangeState)
	);
}


void MathShutdown(void)
{
	FreeMachineServices(providerID);
}
