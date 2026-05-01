
#include "kernel/dispatch.h"
#include "kernel/expression.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"
#include "vm/bytecode.h"		// for service library (should be moved)


void testCallExpression(void)
{
	/**
	 * A CALL_EXPRESSION for the (+ @INT + @INT = $INT) service
	 */

	Atom query = CStringToPredicate("+ 3 + _ = 7");
	ServiceRecord record;
	Tuple * arguments = CreateTuple(3);
	index8 argumentMap[3];
	ASSERT(DispatchQuery(query, &record, arguments, argumentMap))

	Expression callExpression = {
		.type = CALL_EXPRESSION,
		.fields.record = record
	};
	
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
	FreeTuple(arguments);
	IFactRelease(query);	
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	SetupServiceLibrary();

	ExecuteTest(testCallExpression);

	TeardownServiceLibrary();	
	KernelShutdown();

	TestSummary();
}

