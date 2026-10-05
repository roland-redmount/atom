#include "compiler/compiler.h"
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/operator.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "lang/Variable.h"
#include "library/library.h"
#include "library/MachineService.h"
#include "library/string.h"
#include "parser/FormulaBuilder.h"
#include "parser/TermBuilder.h"
#include "ui/query.h"
#include "testing/testing.h"


void testAddInt(void)
{
	Atom query = CStringToTerm("+ 2 + 3 = _");
	FormulaView queryView = FormulaGetView(query);

	Service service;
	index8 permutation[3];
	// This service may need compilation
	ASSERT_TRUE(DispatchOrCompileQuery(queryView, &service, permutation))

	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryView.actors), arguments, 3);
	
	Operator * op = ServiceGetOperator(service);
	ASSERT_NOT_NULL(op)
	void * context = OperatorCreateContext(op, arguments);
	ASSERT_TRUE(OperatorCall(context))
	
	Atom equalsRole = CreateNameFromCString("=");
	index8 equalsRoleIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(queryView.form),
		equalsRole
	);
	NameRelease(equalsRole);
	ASSERT_INT32_EQUAL(arguments[equalsRoleIndex]._int, 2 + 3);

	ASSERT_FALSE(OperatorCall(context))
	
	OperatorFreeContext(context);
	ReleaseFormula(query);
}


void testSubInt(void)
{
	Atom query = CStringToTerm("+ 7 - 4 =  _");

	Service service;
	index8 permutation[3];
	ASSERT_INT32_EQUAL(DispatchQueryFormula(query, &service, permutation), DISPATCH_FOUND)

	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(query)), arguments, 3);

	Operator * op = ServiceGetOperator(service);
	ASSERT_NOT_NULL(op)
	void * context = OperatorCreateContext(op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom equalsRole = CreateNameFromCString("=");
	index8 equalsRoleIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(FormulaGetForm(query)),
		equalsRole
	);
	NameRelease(equalsRole);
	ASSERT_INT32_EQUAL(arguments[equalsRoleIndex]._int, 7 - 4);

	ASSERT_FALSE(OperatorCall(context))
	
	OperatorFreeContext(context);
	ReleaseFormula(query);
}


/**
 * The range service yields one tuple per number in the range, rather than the single
 * tuple an arithmetic service computes.
 */
/* CLAUDE: The range service has the conjunction form (=< >= & =< >=), and the query
 * repeats the variable n in both terms. The two terms have the same form, so the query
 * may list them in either order; dispatch finds the service under a permutation. */
static void assertRange(char const * queryString, int64 lower, int64 upper)
{
	Atom query = CStringToFormula(queryString);
	FormulaView queryView = FormulaGetView(query);
	size32 nServices = NumberOfServices();

	Service service;
	index8 permutation[queryView.actors->nAtoms];
	ASSERT_INT32_EQUAL(DispatchQueryFormula(query, &service, permutation), DISPATCH_FOUND)
	ASSERT_TRUE(HasRepeatedParameters(service.equalitySignature))

	// The column of the first occurrence of n
	index8 numberIndex = 0;
	while(TypedTupleGetElement(queryView.actors, numberIndex).type != AT_VARIABLE)
		numberIndex++;

	MixedTypeRelation * relation = UserQuery(queryView);
	for(int64 expected = lower; expected <= upper; expected++) {
		ASSERT_TRUE(MixedTypeRelationNext(relation))
		TypedTuple const * tuple = MixedTypeRelationPeekTuple(relation);
		ASSERT_INT64_EQUAL(TypedTupleGetAtom(tuple, numberIndex)._int, expected)
	}
	ASSERT_FALSE(MixedTypeRelationNext(relation))
	FreeMixedTypeRelation(relation);

	// The query is answered by the primitive service, so nothing was compiled
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices)
	ReleaseFormula(query);
}


void testRange(void)
{
	assertRange("=< n >= 2 & >= n =< 6", 2, 6);
	assertRange(">= n =< 6 & =< n >= 2", 2, 6);
	assertRange("=< n >= 3 & >= n =< 1", 3, 2);
}


/*
 * CLAUDE: Query (* x / y = q rem r) and check the single resulting tuple.
 */
static void assertIntDivision(char const * queryString, int64 expectedQuotient, int64 expectedRemainder)
{
	Atom query = CStringToTerm(queryString);
	FormulaView queryView = FormulaGetView(query);
	Atom predicateForm = TermFormGetPredicateForm(queryView.form);
	Atom equalsRole = CreateNameFromCString("=");
	Atom remainderRole = CreateNameFromCString("rem");
	index8 quotientIndex = PredicateRoleIndex(predicateForm, equalsRole);
	index8 remainderIndex = PredicateRoleIndex(predicateForm, remainderRole);
	NameRelease(equalsRole);
	NameRelease(remainderRole);

	MixedTypeRelation * relation = UserQuery(queryView);
	ASSERT_TRUE(MixedTypeRelationNext(relation))
	TypedTuple const * tuple = MixedTypeRelationPeekTuple(relation);
	ASSERT_INT64_EQUAL(TypedTupleGetAtom(tuple, quotientIndex)._int, expectedQuotient)
	ASSERT_INT64_EQUAL(TypedTupleGetAtom(tuple, remainderIndex)._int, expectedRemainder)
	ASSERT_FALSE(MixedTypeRelationNext(relation))
	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);
}


/**
 * CLAUDE: Integer division gives the remainder 0 <= r < |y| whatever the signs of x and y.
 */
void testIntDivision(void)
{
	assertIntDivision("* 7 / 2 = q rem r", 3, 1);
	assertIntDivision("* -7 / 2 = q rem r", -4, 1);
	assertIntDivision("* 7 / -2 = q rem r", -3, 1);
	assertIntDivision("* -7 / -2 = q rem r", 4, 1);

	// CLAUDE: division by zero gives no tuple
	Atom query = CStringToTerm("* 7 / 0 = q rem r");
	MixedTypeRelation * relation = UserQuery(FormulaGetView(query));
	ASSERT_FALSE(MixedTypeRelationNext(relation))
	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);
}


/*
 * CLAUDE: Query a term of two parts, given as role names and actors, and return the
 * number of tuples. The actor of role2 in the last tuple is written to result.
 */
static size32 queryTwoParts(
	char const * role1, TypedAtom actor1, char const * role2, TypedAtom actor2, TypedAtom * result)
{
	Atom roles[2] = {CreateNameFromCString(role1), CreateNameFromCString(role2)};
	TypedAtom actors[2] = {actor1, actor2};
	Atom predicate = CreatePredicate(roles, actors, 2);
	Atom query = CreateTerm(predicate, true);
	FormulaView queryView = FormulaGetView(query);
	index8 resultIndex = PredicateRoleIndex(TermFormGetPredicateForm(queryView.form), roles[1]);

	size32 nTuples = 0;
	MixedTypeRelation * relation = UserQuery(queryView);
	while(MixedTypeRelationNext(relation)) {
		*result = TypedTupleGetElement(MixedTypeRelationPeekTuple(relation), resultIndex);
		nTuples++;
	}
	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);
	ReleaseFormula(predicate);
	NameRelease(roles[0]);
	NameRelease(roles[1]);
	return nTuples;
}


static TypedAtom floatAtom(float64 x)
{
	return CreateTypedAtom(AT_FLOAT, (Atom) {._float = x});
}


static TypedAtom intAtom(int64 n)
{
	return CreateTypedAtom(AT_INT, (Atom) {._int = n});
}


static TypedAtom variableAtom(char name)
{
	return CreateTypedAtom(AT_VARIABLE, CreateVariable(name));
}


/*
 * CLAUDE: Query (value x rounded y) for the given x, and return the number of tuples.
 * The rounded value of the last tuple is written to rounded.
 */
static size32 queryRounded(float64 x, int64 * rounded)
{
	TypedAtom result;
	size32 nTuples = queryTwoParts("value", floatAtom(x), "rounded", variableAtom('y'), &result);
	if(nTuples > 0)
		*rounded = result.atom._int;
	return nTuples;
}


/**
 * CLAUDE: Rounding gives the nearest integer, with a halfway case rounded away from
 * zero. A value with no nearest INT gives no tuple.
 */
void testValueRounded(void)
{
	float64 values[] = {1.4, 2.5, -2.5, 0.0, 0.49999999999999994, -0.49999999999999994, 4503599627370497.0};
	int64 expected[] = {1, 3, -3, 0, 0, 0, 4503599627370497};
	for(index8 i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		int64 rounded;
		ASSERT_UINT32_EQUAL(queryRounded(values[i], &rounded), 1)
		ASSERT_INT64_EQUAL(rounded, expected[i])
	}

	int64 rounded;
	ASSERT_UINT32_EQUAL(queryRounded(1e19, &rounded), 0)
	ASSERT_UINT32_EQUAL(queryRounded(-1e19, &rounded), 0)
	ASSERT_UINT32_EQUAL(queryRounded(0.0 / 0.0, &rounded), 0)
}


/*
 * CLAUDE: Count the tuples of a query formula.
 */
static size32 countTuples(char const * queryString)
{
	Atom query = CStringToFormula(queryString);
	MixedTypeRelation * relation = UserQuery(FormulaGetView(query));
	size32 nTuples = 0;
	while(MixedTypeRelationNext(relation))
		nTuples++;
	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);
	return nTuples;
}


/**
 * CLAUDE: (integer n float f) holds if n and f are the same number, for |n| < 2^53.
 */
void testIntegerFloat(void)
{
	int64 const maxExact = ((int64) 1 << 53) - 1;
	TypedAtom result;

	// CLAUDE: from INT to FLOAT
	int64 integers[] = {0, 7, -7, maxExact, -maxExact};
	for(index8 i = 0; i < sizeof(integers) / sizeof(integers[0]); i++) {
		ASSERT_UINT32_EQUAL(queryTwoParts("integer", intAtom(integers[i]), "float", variableAtom('f'), &result), 1)
		ASSERT_DOUBLE_EQUAL(result.atom._float, (float64) integers[i])
	}
	ASSERT_UINT32_EQUAL(queryTwoParts("integer", intAtom(maxExact + 1), "float", variableAtom('f'), &result), 0)
	ASSERT_UINT32_EQUAL(queryTwoParts("integer", intAtom(-maxExact - 1), "float", variableAtom('f'), &result), 0)

	// CLAUDE: from FLOAT to INT. The FLOAT -0.0 is the integer 0.
	float64 floats[] = {7.0, -7.0, -0.0, (float64) maxExact};
	int64 expected[] = {7, -7, 0, maxExact};
	for(index8 i = 0; i < sizeof(floats) / sizeof(floats[0]); i++) {
		ASSERT_UINT32_EQUAL(queryTwoParts("float", floatAtom(floats[i]), "integer", variableAtom('n'), &result), 1)
		ASSERT_INT64_EQUAL(result.atom._int, expected[i])
	}
	float64 notIntegers[] = {2.5, 0x1p53, -0x1p53, 1e19, 1.0 / 0.0, 0.0 / 0.0};
	for(index8 i = 0; i < sizeof(notIntegers) / sizeof(notIntegers[0]); i++)
		ASSERT_UINT32_EQUAL(queryTwoParts("float", floatAtom(notIntegers[i]), "integer", variableAtom('n'), &result), 0)

	// CLAUDE: both given
	ASSERT_UINT32_EQUAL(countTuples("integer 3 float 3.0"), 1)
	ASSERT_UINT32_EQUAL(countTuples("integer 3 float 3.5"), 0)

	// CLAUDE: arithmetic mixing an INT and a FLOAT
	ASSERT_UINT32_EQUAL(countTuples("integer 3 float f & * 1.5 * f = 4.5"), 1)
	ASSERT_UINT32_EQUAL(countTuples("integer 2 float f & * 1.5 / f = 0.75"), 1)
	ASSERT_UINT32_EQUAL(countTuples("integer 2 float f & + 1.5 + f = 3.5"), 1)
}


int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testAddInt);
	ExecuteTest(testSubInt);
	ExecuteTest(testRange);
	ExecuteTest(testIntDivision);
	ExecuteTest(testValueRounded);
	ExecuteTest(testIntegerFloat);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}

