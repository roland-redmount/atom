
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
	Formula * query;
	
	// this query matches with the identity permutation
	query = CStringToTerm("+ 3 + 4 = _");
	index8 permutation[3];
	ASSERT_TRUE(DispatchQueryFormula(query, &record, permutation))
	ASSERT_UINT32_EQUAL(record.service->type, SERVICE_MACHINE)
	FreeFormula(query);

	// one the following two queries requires form permutation to match
	query = CStringToTerm("+ 3 + _ = 7");
	ASSERT_TRUE(DispatchQueryFormula(query, &record, permutation))
	FreeFormula(query);

	query = CStringToTerm("+ _ + 3 = 7");
	ASSERT_TRUE(DispatchQueryFormula(query, &record, permutation))
	FreeFormula(query);
}


/**
 * A variable occurring at several positions of a query denotes one atom, so it
 * can only match service parameters of the same type.
 */
void testDispatchRepeatedVariable(void)
{
	ServiceRecord record;
	index8 permutation[3];
	Formula * query;

	// The service (list <ID position >UINT element >LETTER) has a position of a
	// different type than an element, so no atom can be both
	query = CStringToTerm("list \"ab\" position _x element _x");
	ASSERT_FALSE(DispatchQueryFormula(query, &record, permutation))
	FreeFormula(query);

	// Distinct variables at those same positions match as before
	query = CStringToTerm("list \"ab\" position _p element _e");
	ASSERT_TRUE(DispatchQueryFormula(query, &record, permutation))
	FreeFormula(query);

	// Each occurence of the anonymous variable is a variable of its own
	query = CStringToTerm("list \"ab\" position _ element _");
	ASSERT_TRUE(DispatchQueryFormula(query, &record, permutation))
	FreeFormula(query);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testDispatchToService);
	ExecuteTest(testDispatchRepeatedVariable);

	MathTeardown();
	KernelShutdown();
	TestSummary();
}
