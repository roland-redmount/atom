#include "kernel/dispatch.h"
#include "kernel/service.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
#include "lang/Formula.h"
#include "library/math.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


void testAdd1(void)
{
	Formula * query = CStringToTerm("= _ + 2 + 3");

	ServiceRecord record;
	index8 permutation[3];
	ASSERT(DispatchQueryFormula(query, &record, permutation))

	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(query->actors), arguments, 3);
	
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))
	// TODO: here again we need a systematic way to address the "=" role
	ASSERT_INT32_EQUAL(arguments[0]._int, 2 + 3);

	ASSERT_FALSE(ServiceCall(context))
	
	ServiceFreeContext(context);
	FreeFormula(query);
}


void testAdd2(void)
{
	Formula * query = CStringToTerm("= 7 + 4 + _");

	ServiceRecord record;
	index8 permutation[3];
	ASSERT(DispatchQueryFormula(query, &record, permutation))

	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(query->actors), arguments, 3);
	
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))
	ASSERT_INT32_EQUAL(arguments[2]._uint, 7 - 4);

	ASSERT_FALSE(ServiceCall(context))
	
	ServiceFreeContext(context);
	FreeFormula(query);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testAdd1);
	ExecuteTest(testAdd2);

	MathTeardown();
	KernelShutdown();

	TestSummary();
}

