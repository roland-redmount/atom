#include "kernel/dispatch.h"
#include "kernel/service.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/typedtuple.h"
#include "lang/Formula.h"
#include "library/math.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


void testAdd1(void)
{
	Atom query = CStringToTerm("= _ + 2 + 3");
	Atom actors = FormulaGetActors(query);

	ServiceRecord record;
	index8 permutation[3];
	ASSERT(DispatchQueryFormula(query, &record, permutation))

	TypedTuple * arguments = CreateTypedTuple(3);
	CopyListToTuple(actors, arguments);
	
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))
	ASSERT_INT32_EQUAL(TypedTupleGetAtom(arguments, 0)._int, 2 + 3);

	ASSERT_FALSE(ServiceCall(context))
	
	ServiceFreeContext(context);
	FreeTypedTuple(arguments);

	IFactRelease(query);
}


void testAdd2(void)
{
	Atom query = CStringToTerm("= 7 + 4 + _");
	Atom actors = FormulaGetActors(query);

	ServiceRecord record;
	index8 permutation[3];
	ASSERT(DispatchQueryFormula(query, &record, permutation))

	TypedTuple * arguments = CreateTypedTuple(3);
	CopyListToTuple(actors, arguments);
	
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))
	ASSERT_INT32_EQUAL(TypedTupleGetAtom(arguments, 2)._uint, 7 - 4);

	ASSERT_FALSE(ServiceCall(context))
	
	ServiceFreeContext(context);
	FreeTypedTuple(arguments);

	IFactRelease(query);
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

