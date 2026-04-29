
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "lang/Formula.h"
#include "parser/ClauseBuilder.h"
#include "testing/testing.h"
#include "vm/bytecode.h"


void testDispatchToService(void)
{
	ServiceRecord service;
	Atom query;
	Tuple * arguments = CreateTuple(3);
	
	// this query matches with the identity permutation
	// NOTE: dispatch should probably take a term, not a predicate?
	query = CStringToPredicate("+ 3 + 4 = _");
	ASSERT_TRUE(DispatchQuery(query, &service, arguments));
	ASSERT_UINT32_EQUAL(service.type, SERVICE_BYTECODE);
	IFactRelease(query);

	// one the following two  queries requires form permutation to match
	query = CStringToPredicate("+ 3 + _ = 7");
	ASSERT_TRUE(DispatchQuery(query, &service, arguments));
	ASSERT_UINT32_EQUAL(service.type, SERVICE_BYTECODE);
	IFactRelease(query);

	query = CStringToPredicate("+ _ + 3 = 7");
	ASSERT_TRUE(DispatchQuery(query, &service, arguments));
	ASSERT_UINT32_EQUAL(service.type, SERVICE_BYTECODE);
	IFactRelease(query);

	FreeTuple(arguments);
}


void testDispatchToRule(void)
{
	/**
	 * If we don't have a matching service, but we have a rule
	 * that can define a new service, then we need to compile
	 * a new bytecode service for the query formula and execute it.
	 * This is quite complex, so we should implement it in steps.
	 */

	// the rule (root r square s <- * r * r = s)
	Atom rule = CStringToClause("! * _r * _r = _s | root _r square _s");
	DictionaryAddClause(rule);

	// When dispatch is fully implemented with just-in-time compilation,
	// this should yields newly compiled service via the above rule
	Atom queryTerm = CStringToTerm("root 3 square _s");
	
	ServiceRecord service;
	Tuple * arguments = CreateTuple(3);
	DispatchQuery(queryTerm, &service, arguments);

	DictionaryRemoveClause(rule);
	IFactRelease(rule);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	SetupServiceLibrary();

	ExecuteTest(testDispatchToService);
	// ExecuteTest(testDispatchToRule);

	TeardownServiceLibrary();
	KernelShutdown();

	TestSummary();
}
