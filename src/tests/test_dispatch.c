
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"
#include "vm/bytecode.h"


void testDispatch(void)
{

	Atom query = CStringToPredicate("+ 3 + 4 = _");

	Service service = DispatchQuery(query);
	ASSERT(service.type == SERVICE_BYTECODE);
	
	ReleaseAtom(query);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	SetupCoreServices();

	testDispatch();

	TestSummary();

	TeardownCoreServices();
	KernelShutdown();
}
