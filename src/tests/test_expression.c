
#include "kernel/dispatch.h"
#include "kernel/expression.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "lang/Formula.h"
#include "lang/Variable.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"


void testMachineExpression(void)
{
	/**
	 * A EXPRESSION_MACHINE for the (+ @INT + @INT = $INT) service
	 */

	Atom query = CStringToPredicate("+ 3 + _ = 7");
	ServiceRecord record;
	index8 argumentMap[3];
	ASSERT(DispatchQueryFormula(query, &record, argumentMap))

	// TODO

	IFactRelease(query);	
}


/**
 * Test creating and evaluating the JOIN expression
 * (multiple m element e multiset p) & (predicate-form p)
 * with arguments (p, e, m)
 */
void testJoinExpression1(void)
{
	ServiceRecord const * leftService = RegistryGetCoreServiceRecord(FORM_MULTISET_ELEMENT_MULTIPLE);
	// Left expression: (multiple m element e multiset p)
	Expression leftExpression;
	SetupMachineExpression(
		&leftExpression, 3,
		(index8[]) {2, 1, 0},
		&leftService->expression.value.machineService
	);

	// Right expression: (predicate-form p)
	ServiceRecord const * rightService = RegistryGetCoreServiceRecord(FORM_PREDICATE_FORM);
	Expression rightExpression;
	SetupMachineExpression(
		&rightExpression, 1,
		(index8[]) {0},
		&rightService->expression.value.machineService
	);
	
	// Create the join expression, default argument map
	Expression joinExpression;
	SetupJoinExpression(
		&joinExpression, 3,
		0,
		&leftExpression,
		&rightExpression
	);

	// Evaluate with arguments (@multiset-form, _ , _)
	Tuple * arguments = CreateTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_ID, GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT)),
			anonymousVariable,
			anonymousVariable
		},
		3
	);
	// PrintTuple(arguments);
	// PrintChar('\n');
	// Setup execution context
	void * context = ExpressionCreateContext(&joinExpression, arguments);

	// Call the expression.
	// This should yield 3 tuples corresponding to the 3 roles of (list position element),
 	// since the right expression (predicate-form @multiset-form) matches a single tuple.
	size32 nElements = 0;
	while(ExpressionCall(context)) {
		PrintTuple(arguments);
		PrintChar('\n');
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 3);
	ExpressionFreeContext(context);
	FreeTuple(arguments);
}


/**
 * Test evaluating the join expression
 * (position p list l element s) & (position q list s element e)
 * with arguments (l p s q e)
 */
void testJoinExpression2(void)
{
	ServiceRecord const * listRecord = RegistryGetCoreServiceRecord(FORM_LIST_POSITION_ELEMENT);
	
	// The left and right expressions call the same service,
	// but with different argument mapping.
	// 
	Expression leftExpression;
	SetupMachineExpression(
		&leftExpression, 3,
		(index8[]) {1, 0, 2},
		&listRecord->expression.value.machineService
	);
	Expression rightExpression;
	SetupMachineExpression(
		&rightExpression, 3,
		(index8[]) {3, 2, 4},
		&listRecord->expression.value.machineService
	);

	// Create the join expression 
	Expression joinExpression;
	SetupJoinExpression(&joinExpression, 5, 0, &leftExpression, &rightExpression);

	// Arguments tuple (@stringList _  _ _ _)
	TypedAtom string1 = CreateTypedAtom(AT_ID, CreateStringFromCString("foo"));
	TypedAtom string2 = CreateTypedAtom(AT_ID, CreateStringFromCString("bar"));
	TypedAtom stringList = CreateTypedAtom(AT_ID, CreateListFromArray((TypedAtom[]) {string1, string2}, 2));
	Tuple * arguments = CreateTuple(5);
	TupleSetElement(arguments, 0, stringList);
	for(index8 i = 1; i < 5; i++)
		TupleSetElement(arguments, i, anonymousVariable);
	PrintTuple(arguments);
	PrintChar('\n');

	// Setup execution context
	void * context = ExpressionCreateContext(&joinExpression, arguments);
	// Call the expression
	size32 nElements = 0;
	while(ExpressionCall(context)) {
		PrintTuple(arguments);
		PrintChar('\n');
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 6)
	ExpressionFreeContext(context);
	FreeTuple(arguments);
	ReleaseTypedAtom(stringList);
	ReleaseTypedAtom(string1);
	ReleaseTypedAtom(string2);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	// SetupServiceLibrary();

	// ExecuteTest(testMachineExpression);
	ExecuteTest(testJoinExpression1);
	ExecuteTest(testJoinExpression2);

	// TeardownServiceLibrary();	
	KernelShutdown();

	TestSummary();
}

