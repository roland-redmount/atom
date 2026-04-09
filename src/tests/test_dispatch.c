
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"
#include "vm/bytecode.h"


void testDispatch(void)
{

	Atom query = CStringToPredicate("+ 3 + 4 = _");

	ServiceRecord service = DispatchQuery(query);
	ASSERT(service.type == SERVICE_BYTECODE);
	
	IFactRelease(query);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	SetupCoreServices();

	ExecuteTest(testDispatch);

	TeardownCoreServices();
	KernelShutdown();

	TestSummary();
}
