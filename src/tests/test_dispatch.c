
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"
#include "vm/bytecode.h"


void testDispatch(void)
{
	// this query matches with the identity permutation
	Atom query = CStringToPredicate("+ 3 + 4 = _");

	ServiceRecord service;
	ASSERT_TRUE(DispatchQuery(query, &service));
	ASSERT_UINT32_EQUAL(service.type, SERVICE_BYTECODE);
	
	IFactRelease(query);

	// TODO: define a service s.t. matching requires form permutation,
	// where parameters differ within the same role, e.g.
	// e.g. (+ @INT + $OUT = @INT)
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	SetupServiceLibrary();

	ExecuteTest(testDispatch);

	TeardownCoreServices();
	KernelShutdown();

	TestSummary();
}
