
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "lang/Formula.h"
#include "library/math.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


/**
 * Test dispatching a query to the math service (+ + =)
 */
void testDispatchToService(void)
{
	ServiceRecord record;
	Atom query;
	
	// this query matches with the identity permutation
	query = CStringToTerm("+ 3 + 4 = _");
	index8 permutation[3];
	ASSERT_TRUE(DispatchQueryFormula(query, &record, permutation))
	ASSERT_UINT32_EQUAL(record.service->type, SERVICE_MACHINE)
	IFactRelease(query);

	// one the following two queries requires form permutation to match
	query = CStringToTerm("+ 3 + _ = 7");
	ASSERT_TRUE(DispatchQueryFormula(query, &record, permutation))
	IFactRelease(query);

	query = CStringToTerm("+ _ + 3 = 7");
	ASSERT_TRUE(DispatchQueryFormula(query, &record, permutation))
	IFactRelease(query);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testDispatchToService);

	MathTeardown();
	KernelShutdown();
	TestSummary();
}
