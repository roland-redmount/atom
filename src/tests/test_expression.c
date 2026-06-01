
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
	ASSERT(DispatchQuery(query, &record, argumentMap))

	// Expression callExpression = {
	// 	.type = CALL_EXPRESSION,
	// 	.fields.record = record
	// };
	
	// TODO
/*
	// evaluate the expression
	EvaluationContext context;
	ExpressionIterate(&callExpression, arguments, argumentMap, &context);

	ASSERT_TRUE(ExpressionNext(&context))
	PrintTuple(arguments);

	ASSERT_FALSE(ExpressionNext(&context))

	ExpressionEnd(&context);
*/

	IFactRelease(query);	
}


/**
 * Test evaluatiing the join expression (multiple m element e multiset p) & (predicate-form p)
 * Here the right hand expression (predicate-form p) has only a single tuple
 */
void testJoinExpression1(void)
{
	ServiceRecord const * left = RegistryGetCoreServiceRecord(FORM_MULTISET_ELEMENT_MULTIPLE);
	ServiceRecord const * right = RegistryGetCoreServiceRecord(FORM_PREDICATE_FORM);
	
	// Crete the join expression (multiple m element e multiset p) & (predicate-form p)
	// with argument mapping (m e p) <- (m e p) & (p).
	// The indexes must correspond to the form canonical order
	Expression joinExpression;
	CreateJoinExpression(
		&joinExpression, 3,
		&(left->expression), (index8[]) {0, 1, 2},
		&(right->expression), (index8[]) {2}
	);

	// Argument list
	Tuple * arguments = CreateTuple(3);
	MultisetSetTuple(arguments,
		CreateTypedAtom(AT_ID, GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT)),
		anonymousVariable,
		anonymousVariable
	);
	// PrintTuple(arguments);
	// PrintChar('\n');
	// Setup execution context
	void * context = ExpressionCreateContext(&joinExpression, arguments);

	// Call the expression
	// this should yields 3 elements corresponding to the 3 roles of (list position element)
	size32 nElements = 0;
	while(ExpressionCall(&joinExpression, context)) {
		PrintTuple(arguments);
		PrintChar('\n');
		nElements++;
	}
	ExpressionFreeContext(&joinExpression, context);
	FreeTuple(arguments);
}


/**
 * Test evaluatiing the join expression
 * (position p list l element s) & (position q list s element e)
 */
void testJoinExpression2(void)
{
	// Create a list of two strings
	TypedAtom string1 = CreateTypedAtom(AT_ID, CreateStringFromCString("foo"));
	TypedAtom string2 = CreateTypedAtom(AT_ID, CreateStringFromCString("bar"));
	TypedAtom stringList = CreateTypedAtom(AT_ID, CreateListFromArray((TypedAtom[]) {string1, string2}, 2));

	ServiceRecord const * listRecord = RegistryGetCoreServiceRecord(FORM_LIST_POSITION_ELEMENT);
	
	// Crete the join expression 
	// (position p list l element s) & (position q list s element e)
	// with argument mapping (p l s q e) <- (p l s) & (q s e)
	Expression joinExpression;
	CreateJoinExpression(
		&joinExpression, 5,
		&(listRecord->expression), (index8[]) {0, 1, 2},
		&(listRecord->expression), (index8[]) {3, 2, 4}
	);

	// Arguments (_ @stringList _  _ _)
	Tuple * arguments = CreateTuple(5);
	for(index8 i = 0; i < 5; i++)
		TupleSetElement(arguments, i, anonymousVariable);
	TupleSetElement(arguments, 1, stringList);
	// PrintTuple(arguments);
	// PrintChar('\n');
	// Setup execution context
	void * context = ExpressionCreateContext(&joinExpression, arguments);

	// Call the expression
	size32 nElements = 0;
	while(ExpressionCall(&joinExpression, context)) {
		PrintTuple(arguments);
		PrintChar('\n');
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 6)
	ExpressionFreeContext(&joinExpression, context);
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

