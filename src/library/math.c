
#include "kernel/dictionary.h"
#include "library/MachineService.h"
#include "library/math.h"
#include "parser/TermBuilder.h"


//------------------------------- Integer arithmetic ------------------------------------------

/**
 * Type predicate (integer #1<INT)
 * 
 * NOTE: a FLOAT atom with zero decimals like 42.0 is not considered an integer.
 * Floating points numbers are not considered rational numbers, but approximate values.
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
 * Addition x + y = z
 */
static bool addIntCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._int = arguments[0]._int + arguments[1]._int;
	return true;
}

/**
 * (+ x<INT - y<INT = z>INT)
 * 
 * Subtraction x - y = z
 */
static bool subIntCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._int = arguments[0]._int - arguments[1]._int;
	return true;
}

/**
 * (* x<INT * y<INT = z>INT)
 * 
 * Multiplication x * y = z
 */
static bool mulIntCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._int = arguments[0]._int * arguments[1]._int;
	return true;
}

/**
 * (* x<INT / y<INT = q>INT rem r>INT)
 * 
 * Integer division x / y = q with remainder r. We use Euclidean division, which
 * constrains 0 <= r < |y|. Setting y = 0 yields no tuple.
 * 
 * NOTE: an alternative name is (dividend x<INT divisor y<INT quotient q>INT remainder r>INT)
 */
static bool intDivisionCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	int64 x = arguments[0]._int;
	int64 y = arguments[1]._int;
	if(y == 0)
		return false;
	if(x == INT64_MIN && y == -1) {
		// the division would overflow
		return false;
	}
	int64 q = x / y;
	int64 r = x % y;

	if(r < 0) {
		// C99 rounds the quotient towards zero, which can give negative remainder,
		// e.g. -7 / 2 yields q = -3, r = -1 instead of q = -4, r = 1
		// Correct for this.
		if(y > 0) {
			q--;
			r += y;
		}
		else {
			q++;
			r -= y;
		}
	}
	arguments[2]._int = q;
	arguments[3]._int = r;
	return true;
}


/**
 * (* x<INT * y>INT = z<INT)
 * 
 * Solving the equation x * y = z for y. For x != 0 this holds iff y = z / x with remainder 0.
 */


//------------------------------- Floating point arithmetic ------------------------------------

/**
 * (+ x<FLOAT + y<FLOAT = z>FLOAT)
 */
static bool addFloatCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._float = arguments[0]._float + arguments[1]._float;
	return true;
}

/**
 * (+ x<FLOAT - y<FLOAT = z>FLOAT)
 */
static bool subFloatCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._float = arguments[0]._float - arguments[1]._float;
	return true;
}

/**
 * (* x<FLOAT * y<FLOAT = z>FLOAT)
 */
static bool mulFloatCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._float = arguments[0]._float * arguments[1]._float;
	return true;
}

/**
 * (* x<FLOAT / y<FLOAT = z>FLOAT)
 * 
 * Floating point division. Returns no tuple if y = 0
 */
static bool divFloatCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	float64 x = arguments[0]._float;
	float64 y = arguments[1]._float;
	if(y == 0)
		return false;
	arguments[2]._float = x / y;
	return true;
}

/**
 * (value x<FLOAT rounded y>INT)
 * 
 * Round a floating point value to the nearest integer.
 * A halfway value (0.5, -0.5, etc) rounds away from zero; see RoundFloat64().
 * A value whose nearest integer is outside the range of an INT gives no tuple, and so does NaN.
 */
static bool valueRoundedCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	float64 x = arguments[0]._float;
	// CLAUDE: 0x1p63 is 2^63. NaN fails both comparisons.
	if(!((x >= -0x1p63) && (x < 0x1p63)))
		return false;
	arguments[1]._int = (int64) RoundFloat64(x);
	return true;
}


/**
 * (value x<FLOAT scale k<INT scaled y>FLOAT)
 * 
 * Scalar multiplication of a FLOAT x by an INT k. This cannot use symmetric roles (* * =)
 * since the type of the two * roles differ: this is not the * operator of a field, but
 * rather scalar multiplication of x by k (which is outside the field) to obtain y.
 * See https://en.wikipedia.org/wiki/Field_(mathematics)
 */
static bool valueScaleScaledCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._float = arguments[0]._float * arguments[1]._int;
	return true;
}

/**
 * (value x<FLOAT invscale k<INT scaled y>FLOAT)
 * 
 * The inverse scalar operation y = x / k, for k != 0.
 * Note that, since floating point arithmetic is inexact, it may not hold that
 * (value x scale k scaled y) <-> (value y invscale k scaled x),
 * and so the join relation (value x scale k scaled y & value y invscale k scaled x)
 * may be empty. An example for 64-bit floats is x = 0.003 and k = 3.
 * Therefore, we cannot use (value #1>FLOAT scale #2<INT scaled #3<FLOAT) to express
 * the inverse scaling.
 */
static bool valueInvscaleScaledCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	float64 x = arguments[0]._float;
	int64 k = arguments[1]._int;
	if(k == 0)
		return false;
	arguments[2]._float = x / k;
	return true;
}

//------------------------------------- Inequalities ------------------------------------------

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

	// Integer arithmetic
	RegisterMachineService(moduleID, "integer #1<INT", integerCall);
	RegisterMachineService(moduleID, "+ #1<INT + #2<INT = #3>INT", addIntCall);
	RegisterMachineService(moduleID, "+ #1<INT - #2<INT = #3>INT", subIntCall);
	RegisterMachineService(moduleID, "* #1<INT * #2<INT = #3>INT", mulIntCall);
	RegisterMachineService(moduleID, "* #1<INT / #2<INT = #3>INT rem #4>INT", intDivisionCall);

	// Floating point arithmetic
	RegisterMachineService(moduleID, "+ #1<FLOAT + #2<FLOAT = #3>FLOAT", addFloatCall);
	RegisterMachineService(moduleID, "+ #1<FLOAT - #2<FLOAT = #3>FLOAT", subFloatCall);
	RegisterMachineService(moduleID, "* #1<FLOAT * #2<FLOAT = #3>FLOAT", mulFloatCall);
	RegisterMachineService(moduleID, "* #1<FLOAT / #2<FLOAT = #3>FLOAT", divFloatCall);

	RegisterMachineService(moduleID, "value #1<FLOAT rounded #2>INT", valueRoundedCall);
	
	// float-by-integer scaling
	RegisterMachineService(moduleID, "value #1<FLOAT scale #2<INT scaled #3>FLOAT", valueScaleScaledCall);
	RegisterMachineService(moduleID, "value #1<FLOAT invscale #2<INT scaled #3>FLOAT", valueInvscaleScaledCall);

	// inequalities
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

	// Let the compiler create (+ #1<INT + #2>INT = #3>INT) from (+ #1<INT - #2<INT = #3>INT)
	// NOTE: this leaves the (+ + =) primitive service stale, needs compilation
	addSubRule = DictionaryAddClauseFromCString("+ x + y = z | ! + z - x = y");
}


void MathShutdown(void)
{
	DictionaryRemoveClause(&addSubRule);

	FreeModuleRelations(moduleID);
}
