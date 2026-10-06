
#include "kernel/dictionary.h"
#include "library/MachineService.h"
#include "library/math.h"
#include "parser/TermBuilder.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "memory/references.h"


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


//---------------------------- Integer-float conversion -------------------------------------

/*
 * 2^53 -1 is the largest integer such that every integer from -n to n is exactly a float64.
 * For larger integers, a float64 may be the rounded value of a different integer:
 * 2^53 + 1 rounds to 2^53.
 */
#define MAX_EXACT_INTEGER	(((int64) 1 << 53) - 1)

/**
 * (integer n<INT float f>FLOAT)
 *
 * The relation (integer n float f) holds if the INT n and the FLOAT f are the same
 * number, for -MAX_EXACT_INTEGER <= n <= MAX_EXACT_INTEGER. An INT outside that range
 * gives no tuple.
 */
static bool integerFloatCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	int64 n = arguments[0]._int;
	if((n < -MAX_EXACT_INTEGER) || (n > MAX_EXACT_INTEGER))
		return false;
	arguments[1]._float = (float64) n;
	return true;
}

/**
 * (integer n>INT float f<FLOAT)
 *
 * The relation (integer n float f) for a given FLOAT f; see integerFloatCall(). A FLOAT
 * that is not an integer, or is outside the range, gives no tuple. To round a FLOAT to
 * an INT, use (value x rounded y) instead.
 *
 * The FLOAT -0.0 is the integer 0, so it gives n = 0. However, the FLOAT for n = 0 is
 * 0.0, and atoms are compared by their bits, so -0.0 and 0.0 are different FLOAT atoms.
 * Hence (integer n float -0.0) gives n = 0, but (integer 0 float -0.0) does not hold.
 */
static bool floatIntegerCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	float64 f = arguments[1]._float;
	// CLAUDE: NaN fails both comparisons, and an infinity fails one
	if(!((f >= -MAX_EXACT_INTEGER) && (f <= MAX_EXACT_INTEGER)))
		return false;
	if(RoundFloat64(f) != f)
		return false;
	arguments[0]._int = (int64) f;
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


static NamedFunction const mathFunctions[] = {
	{"math.integerCall", (AnyFunction) integerCall},
	{"math.addIntCall", (AnyFunction) addIntCall},
	{"math.subIntCall", (AnyFunction) subIntCall},
	{"math.mulIntCall", (AnyFunction) mulIntCall},
	{"math.intDivisionCall", (AnyFunction) intDivisionCall},
	{"math.addFloatCall", (AnyFunction) addFloatCall},
	{"math.subFloatCall", (AnyFunction) subFloatCall},
	{"math.mulFloatCall", (AnyFunction) mulFloatCall},
	{"math.divFloatCall", (AnyFunction) divFloatCall},
	{"math.valueRoundedCall", (AnyFunction) valueRoundedCall},
	{"math.integerFloatCall", (AnyFunction) integerFloatCall},
	{"math.floatIntegerCall", (AnyFunction) floatIntegerCall},
	{"math.strictInequalityIntCall", (AnyFunction) strictInequalityIntCall},
	{"math.strictInequalityFloatCall", (AnyFunction) strictInequalityFloatCall},
	{"math.nonStrictInequalityIntCall", (AnyFunction) nonStrictInequalityIntCall},
	{"math.nonStrictInequalityFloatCall", (AnyFunction) nonStrictInequalityFloatCall},
	{"math.rangeSetup", (AnyFunction) rangeSetup},
	{"math.rangeCall", (AnyFunction) rangeCall},
};


void RegisterMathFunctions(void)
{
	RegisterFunctions(mathFunctions, sizeof(mathFunctions) / sizeof(NamedFunction));
}


#define N_MATH_RULES	5

typedef struct s_MathLibrary {
	uint32 moduleID;
	FormulaView mathRules[N_MATH_RULES];
} MathLibrary;


static MathLibrary * mathLibrary = 0;


void MathSetup(void)
{
	mathLibrary = Allocate(sizeof(MathLibrary));
	SetPersistentState(STATE_KEY_MATH, mathLibrary);

	mathLibrary->moduleID = RequestModuleID();

	// Integer arithmetic
	RegisterMachineService(mathLibrary->moduleID, "integer #1<INT", integerCall);
	RegisterMachineService(mathLibrary->moduleID, "+ #1<INT + #2<INT = #3>INT", addIntCall);
	RegisterMachineService(mathLibrary->moduleID, "+ #1<INT - #2<INT = #3>INT", subIntCall);
	RegisterMachineService(mathLibrary->moduleID, "* #1<INT * #2<INT = #3>INT", mulIntCall);
	RegisterMachineService(mathLibrary->moduleID, "* #1<INT / #2<INT = #3>INT rem #4>INT", intDivisionCall);

	// Floating point arithmetic
	RegisterMachineService(mathLibrary->moduleID, "+ #1<FLOAT + #2<FLOAT = #3>FLOAT", addFloatCall);
	RegisterMachineService(mathLibrary->moduleID, "+ #1<FLOAT - #2<FLOAT = #3>FLOAT", subFloatCall);
	RegisterMachineService(mathLibrary->moduleID, "* #1<FLOAT * #2<FLOAT = #3>FLOAT", mulFloatCall);
	RegisterMachineService(mathLibrary->moduleID, "* #1<FLOAT / #2<FLOAT = #3>FLOAT", divFloatCall);

	RegisterMachineService(mathLibrary->moduleID, "value #1<FLOAT rounded #2>INT", valueRoundedCall);
	
	// integer-float conversion
	RegisterMachineService(mathLibrary->moduleID, "integer #1<INT float #2>FLOAT", integerFloatCall);
	RegisterMachineService(mathLibrary->moduleID, "integer #1>INT float #2<FLOAT", floatIntegerCall);

	// inequalities
	RegisterMachineService(mathLibrary->moduleID, "< #1<INT > #2<INT", strictInequalityIntCall);
	RegisterMachineService(mathLibrary->moduleID, "< #1<FLOAT > #2<FLOAT", strictInequalityFloatCall);

	RegisterMachineService(mathLibrary->moduleID, "=< #1<INT >= #2<INT", nonStrictInequalityIntCall);
	RegisterMachineService(mathLibrary->moduleID, "=< #1<FLOAT >= #2<FLOAT", nonStrictInequalityFloatCall);

	// The range a =< n =< b is the conjunction (n >= a & b >= n), since
	// (=< x >= y) reads x >= y. Neither term is a finite relation on its own.
	RegisterMachineServiceWithState(
		mathLibrary->moduleID, "=< #2>INT >= #1<INT & >= #2>INT =< #3<INT", sizeof(RangeState),
		rangeSetup,	rangeCall, 0
	);

	// Add rules to let the compiler create derived services.
	// NOTE: adding these rules leaves various primitive service stale, needs compilation
	
	// integer division, with zero remainder
	mathLibrary->mathRules[0] = DictionaryAddClauseFromCString("* x / y = z | ! * x / y = z rem 0");

	// Solving x + y = z for y
	mathLibrary->mathRules[1] = DictionaryAddClauseFromCString("+ x + y = z | ! + z - x = y");
	// Solving x * y = z for y. For integers, this requires division with zero remainder
	mathLibrary->mathRules[2] = DictionaryAddClauseFromCString("* x * y = z | ! * z / x = y");

	// Mixed float-int arithmetic. These rules will yield services only for FLOAT x and INT i
	mathLibrary->mathRules[3] = DictionaryAddClauseFromCString("+ x + i = y | ! integer i float f | ! + x + f = y");
	mathLibrary->mathRules[4] = DictionaryAddClauseFromCString("* x * i = y | ! integer i float f | ! * x * f = y");
}


void MathRestore(void)
{
	mathLibrary = GetPersistentState(STATE_KEY_MATH);
	ASSERT(mathLibrary)
}


void MathShutdown(void)
{
	for(index32 i = 0; i < N_MATH_RULES; i++)
		DictionaryRemoveClause(&mathLibrary->mathRules[i]);

	FreeModuleRelations(mathLibrary->moduleID);

	Free(mathLibrary);
	SetPersistentState(STATE_KEY_MATH, 0);
	mathLibrary = 0;
}
