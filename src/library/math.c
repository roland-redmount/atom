
#include "library/MachineService.h"
#include "library/math.h"
#include "parser/TermBuilder.h"


/**
 * (+ x<INT + y<INT = z>INT)
 * 
 * Addition x + y
 */
static bool addIntCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._int = arguments[0]._int + arguments[1]._int;
	return true;
}

static bool addFloatCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._float = arguments[0]._float + arguments[1]._float;
	return true;
}


/**
 * (+ x<INT + y>INT = z<INT)
 * 
 * Solving the additive equation x + y = z for z
 * This can be use to implement subtraction via the rule
 * z = x + y  <->  y = z - x
 * 
 * TODO: this is kind of backwards, as the primitive does subtraction.
 */
static bool addSolveIntCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[1]._int = arguments[2]._int - arguments[0]._int;
	return true;
}

static bool addSolveFloatCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[1]._float = arguments[2]._float - arguments[0]._float;
	return true;
}


/**
 * (* x<INT * y<INT = z>INT)
 * 
 * Multiplication x * y
 */
static bool mulIntCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._int = arguments[0]._int * arguments[1]._int;
	return true;
}

static bool mulFloatCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._float = arguments[0]._float * arguments[1]._float;
	return true;
}

/**
 * (< x<INT > y>INT)
 * 
 * Strict inequality test x > y
 */
static bool strictInequalityIntCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	return arguments[0]._int > arguments[1]._int;
}

static bool strictInequalityFloatCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	return arguments[0]._float > arguments[1]._float;
}



/**
 * (=< x<INT >= y>INT)
 * 
 * Non-strict inequality test x >= y
 */
static bool nonStrictInequalityIntCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	return arguments[0]._int >= arguments[1]._int;
}

static bool nonStrictInequalityFloatCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	return arguments[0]._float >= arguments[1]._float;
}


/**
 * A "co-routine" machine function, returning multiple values.
 * This implements a range iterator (=< #2>INT >= #1<INT & >= #2>INT =< #3<INT)
 * which returns all values #2 between the lower and upper bound, inclusive.
 * The state holds the value returned by the previous call.
 *
 * Successive tuples differ only in #2, which ascends, so the tuples are ordered
 * as RegisterMachineService() requires.
 * 
 */
typedef struct {
	Atom number;
} RangeState;

// initialize state to the lower value
static void rangeSetup(void * state, Atom arguments[], void * readerData, void * storage)
{
	RangeState * rangeState = state;
	rangeState->number = arguments[0];
}


static bool rangeCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	RangeState * rangeState = state;
	if(rangeState->number._int > arguments[2]._int)
		return false;
	arguments[1] = rangeState->number;
	rangeState->number._int++;
	return true;
}


static uint32 moduleID;

void MathSetup(void)
{
	moduleID = RequestModuleID();

	RegisterMachineService(moduleID, "+ #1<INT + #2<INT = #3>INT", addIntCall);
	RegisterMachineService(moduleID, "+ #1<FLOAT + #2<FLOAT = #3>FLOAT", addFloatCall);

	RegisterMachineService(moduleID, "+ #1<INT + #2>INT = #3<INT", addSolveIntCall);
	RegisterMachineService(moduleID, "+ #1<FLOAT + #2>FLOAT = #3<FLOAT", addSolveFloatCall);

	RegisterMachineService(moduleID, "* #1<INT * #2<INT = #3>INT", mulIntCall);
	RegisterMachineService(moduleID, "* #1<FLOAT * #2<FLOAT = #3>FLOAT", mulFloatCall);

	RegisterMachineService(moduleID, "< #1<INT > #2<INT", strictInequalityIntCall);
	RegisterMachineService(moduleID, "< #1<FLOAT > #2<FLOAT", strictInequalityFloatCall);

	RegisterMachineService(moduleID, "=< #1<INT >= #2<INT", nonStrictInequalityIntCall);
	RegisterMachineService(moduleID, "=< #1<FLOAT >= #2<FLOAT", nonStrictInequalityFloatCall);

	// The range a =< n =< b is the conjunction (n >= a & b >= n), since
	// (=< x >= y) reads x >= y. Neither term is a finite relation on its own.
	RegisterMachineServiceWithState(
		moduleID, "=< #2>INT >= #1<INT & >= #2>INT =< #3<INT", sizeof(RangeState),
		rangeSetup,	rangeCall, 0
	);

}


void MathShutdown(void)
{
	FreeModuleRelations(moduleID);
}
