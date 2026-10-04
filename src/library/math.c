
#include "kernel/dictionary.h"
#include "library/MachineService.h"
#include "library/math.h"
#include "parser/TermBuilder.h"

/**
 * Type predicate (integer #1>INT)
 * 
 * NOTE: a FLOAT atom with zero decimals like 42.0 is not considered an integer.
 * Floating points numbers are not considred rational numbers, but approximate values.
 * We would need a different type for rational arithmetic.
 */
static bool integerCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	// if the query dispatches to this service, the argument must be an INT
	return true;
}


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
 * Subtraction (+ x<INT - y<INT = z>INT)
 */
static bool subIntCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._int = arguments[0]._int - arguments[1]._int;
	return true;
}

static bool subFloatCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._float = arguments[0]._float - arguments[1]._float;
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
 * (value #1<FLOAT scale #2<INT scaled #3>FLOAT)
 * 
 * Scalar multiplication of a FLOAT x by an INT y. This cannot use symmetric roles (* * =)
 * since the type of the two * roles differ: this is not the * operator of a field, but
 * rather scalar multiplication of x by k (which it outside the field) to obtain y.
 * See https://en.wikipedia.org/wiki/Field_(mathematics)
 */
static bool valueScaleScaledCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._float = arguments[0]._float * arguments[1]._int;
	return true;
}

/**
 * (value #1<FLOAT invscale #2<INT scaled #3>FLOAT)
 * 
 * The inverse scalar operation. Note that, since floating point arithmetic is inexact,
 * is may not hold that (value x scale k scaled y) <-> (value y invscale k scaled x),
 * and so the join relation (value x scale k scaled y & value y inscale k scaled x)
 * may be empty. An example for 64-bit floats is x = 0.003 and k = 3.
 * Therefore, we cannot use (value #1>FLOAT scale #2<INT scaled #3<FLOAT) to express
 * the inverse scaling.
 */
static bool valueInvscaleScaledCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._float = arguments[0]._float / arguments[1]._int;
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

FormulaView addSubRule;


void MathSetup(void)
{
	moduleID = RequestModuleID();

	RegisterMachineService(moduleID, "integer #1<INT", integerCall);

	RegisterMachineService(moduleID, "+ #1<INT + #2<INT = #3>INT", addIntCall);
	RegisterMachineService(moduleID, "+ #1<FLOAT + #2<FLOAT = #3>FLOAT", addFloatCall);

	RegisterMachineService(moduleID, "+ #1<INT - #2<INT = #3>INT", subIntCall);
	RegisterMachineService(moduleID, "+ #1<FLOAT - #2<FLOAT = #3>FLOAT", subFloatCall);

	RegisterMachineService(moduleID, "* #1<INT * #2<INT = #3>INT", mulIntCall);
	RegisterMachineService(moduleID, "* #1<FLOAT * #2<FLOAT = #3>FLOAT", mulFloatCall);

	RegisterMachineService(moduleID, "value #1<FLOAT scale #2<INT scaled #3>FLOAT", valueScaleScaledCall);
	RegisterMachineService(moduleID, "value #1<FLOAT invscale #2<INT scaled #3>FLOAT", valueInvscaleScaledCall);

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

	// Let the compiler create (+ #1<INT + #2>INT = #3>INT) from ()"+ #1<INT - #2<INT = #3>INT")
	// NOTE: this leaves the (+ + =) primitive service stale, needs compilation
	addSubRule = DictionaryAddClauseFromCString("+ x + y = z | ! + z - x = y");
}


void MathShutdown(void)
{
	DictionaryRemoveClause(&addSubRule);

	FreeModuleRelations(moduleID);
}
