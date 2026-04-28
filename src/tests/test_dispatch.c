
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "lang/Formula.h"
#include "parser/PredicateBuilder.h"
#include "parser/ClauseBuilder.h"
#include "testing/testing.h"
#include "vm/bytecode.h"


void testDispatchToService(void)
{
	ServiceRecord service;
	Atom query;
	
	// this query matches with the identity permutation
	query = CStringToPredicate("+ 3 + 4 = _");
	ASSERT_TRUE(DispatchQuery(query, &service));
	ASSERT_UINT32_EQUAL(service.type, SERVICE_BYTECODE);
	IFactRelease(query);

	// one the following two  queries requires form permutation to match
	query = CStringToPredicate("+ 3 + _ = 7");
	ASSERT_TRUE(DispatchQuery(query, &service));
	ASSERT_UINT32_EQUAL(service.type, SERVICE_BYTECODE);
	IFactRelease(query);

	query = CStringToPredicate("+ _ + 3 = 7");
	ASSERT_TRUE(DispatchQuery(query, &service));
	ASSERT_UINT32_EQUAL(service.type, SERVICE_BYTECODE);
	IFactRelease(query);
}


void testDispatchToRule(void)
{
	/**
	 * If we don't have a matching service, but we have a rule
	 * that can define a new service, then we need to compile
	 * a new bytecode service for the query formula and execute it.
	 * This is quite complex, so we should implement it in steps.
	 */

	// Create the rule
	//   number x square s <- * x * x = s
	// This requires AssertFact() to handle clauses.
	Atom rule = CStringToClause("!number x square s | * x * x = s");
	// AssertFact(FormulaGetForm(rule), FormulaGetActors(rule));

	/**
	 * To match a predicate to a rule, we need to
	 * (1) find a clause form where its form occurs negated
	 * (2) find rules (facts) of that clause form where the 
	 *     corresponding predicate unifies with the query predicate
	 * (3) take the remainder of the clause (unified) and repeat from 1
	 *     until we reach resolution or there are no more matches
	 * Lookup can be used for (1) and (2)
	 * 
	 * The result should be an equivalent formula. For the case
	 * (number 3 square s), we should find (* 3 * 3 = s), which maps
	 * to the service (* @INT * @INT = $INT)
	 * 
	 * Because services are valid for any parameter values that match
	 * the specified atom types, we can immediately generalize this by
	 * substiting constants for the corresponding parameter:
	 * (number @INT square $) --> (* @INT * @INT = s) which unifies
	 * with the service (* @INT * @INT = $INT) to yield a new service
	 * (number @INT square $INT). This can be compiled and then
	 */

	Atom query = CStringToPredicate("number 3 square s");
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	SetupServiceLibrary();

	ExecuteTest(testDispatchToService);

	TeardownServiceLibrary();
	KernelShutdown();

	TestSummary();
}
