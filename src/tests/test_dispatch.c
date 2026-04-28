
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
	Tuple * arguments = CreateTuple(3);
	
	// this query matches with the identity permutation
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

	/**
	 *  Create the rule
	 * 
	 *   number x square s <- * x * x = s
     *
	 * AssertFact() expects facts to come from a service, but a rule
	 * is not a service. So we need something else, say AssertRule().
	 * 
	 * Since we expect each clause form to contain only a few rules,
	 * but we will have many forms, it might make sense to store all rules
	 * in a single B-tree indexed by form and then actors, similar to
	 * ServiceRegistry (but here actors are not parameter lists).
	 * Or, we might store all rules of the same arity in one tree,
	 * so that the tuple size is constant.
	 * 
	 * To search find rules (clauses) c that contain a given @term-form,
	 * we first query (clause-form c) & (multiset c element @term_form multiple _)
	 * We then search for tuples
	 */
	Atom rule = CStringToClause("!number x square s | * x * x = s");
	// AssertRule(rule);

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
