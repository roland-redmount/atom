
#include "library/MachineService.h"
#include "library/math.h"


/**
 * (+ x<INT + y<INT = z>INT)
 * 
 * Addition x + y
 */
static bool add1Call(void * state, Atom arguments[], void * operatorData)
{
	arguments[2]._int = arguments[0]._int + arguments[1]._int;
	return true;
}


/**
 * (+ x<INT + y>INT = z<INT)
 * 
 * Solving the additive equation x + y = z for z
 * This cane be use to implement subtraction via the rule
 * z = x + y  <->  y = z - x
 */
static bool add2Call(void * state, Atom arguments[], void * operatorData)
{
	arguments[1]._int = arguments[2]._int - arguments[0]._int;
	return true;
}


/**
 * (* x<INT * y<INT = z>INT)
 * 
 * Multiplication x * y
 */
static bool mul1Call(void * state, Atom arguments[], void * operatorData)
{
	arguments[2]._int = arguments[0]._int * arguments[1]._int;
	return true;
}

/**
 * (< x<INT > y>INT)
 * 
 * Strict inequality test x > y
 */
static bool strictInequalityCall(void * state, Atom arguments[], void * operatorData)
{
	return arguments[0]._int > arguments[1]._int;
}


/**
 * (=< x<INT >= y>INT)
 * 
 * Non-strict inequality test x >= y
 */
static bool nonStrictInequalityCall(void * state, Atom arguments[], void * operatorData)
{
	return arguments[0]._int >= arguments[1]._int;
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
		"+ @1<INT + @2<INT = @3>INT",
		(MachineOperatorSpec) {.providerID = providerID, .call = add1Call});

	RegisterMachineService(
		"+ @1<INT + @2>INT = @3<INT", 
		(MachineOperatorSpec) {.providerID = providerID, .call = add2Call});

	RegisterMachineService(
		"* @1<INT * @2<INT = @3>INT",
		(MachineOperatorSpec) {.providerID = providerID, .call = mul1Call});

	RegisterMachineService(
		"lower @1<INT number @2>INT upper @3<INT",
		(MachineOperatorSpec) {
			.providerID = providerID,
			.stateSize = sizeof(RangeState),
			.setupState = rangeSetup,
			.call = rangeCall
		}
	);

	RegisterMachineService(
		"< @1<INT > @2<INT",
		(MachineOperatorSpec) {.providerID = providerID, .call = strictInequalityCall});

	RegisterMachineService(
		"=< @1<INT >= @2<INT",
		(MachineOperatorSpec) {.providerID = providerID, .call = nonStrictInequalityCall});
}


void MathShutdown(void)
{
	FreeMachineServices(providerID);
}
