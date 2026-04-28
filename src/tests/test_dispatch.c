
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "lang/Formula.h"
#include "parser/ClauseBuilder.h"
#include "testing/testing.h"
#include "vm/bytecode.h"

#include "kernel/multiset.h"
#include "kernel/RelationBTree.h"
#include "lang/Variable.h"
#include "lang/ClauseForm.h"

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


	Atom rule = CStringToClause("!root _r square _s | * _r * _r = _s");
	DictionaryAddClause(rule);

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

	// This is the negated form
	Atom queryTerm = CStringToTerm("!root 3 square _s");
	Atom queryTermForm = FormulaGetForm(queryTerm);
	ASSERT(IsTermForm(queryTermForm))
	/**
	 * To search find rules (clauses) c that contain a given @term-form,
	 * we first query (clause-form c) & (multiset c element @term_form multiple _)
	 * We then search for tuples
	 */

	RelationBTreeIterator iterator;
	BTree * btree = RegistryGetCoreTable(FORM_MULTISET_ELEMENT_MULTIPLE);

	Tuple * queryTuple = CreateTuple(3);
	MultisetSetTuple(
		queryTuple,
		anonymousVariable,
		(TypedAtom) {.type = AT_ID, .atom = queryTermForm},
		anonymousVariable
	);

	RelationBTreeIterate(btree, queryTuple, &iterator);
	while(RelationBTreeIteratorHasTuple(&iterator)) {
		TypedAtom clauseForm = RelationBTreeIteratorGetAtom(
			&iterator,
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)
		);
		if(clauseForm.type == AT_ID && IsClauseForm(clauseForm.atom)) {
			PrintClauseForm(clauseForm.atom);
			PrintChar('\n');

			DictionaryIterator dictIterator;
			DictionaryIterate(clauseForm.atom, &dictIterator);
			while(DictionaryIteratorHasRecord(&dictIterator)) {
				Tuple const * clauseActors = DictionaryIteratorPeekActors(&dictIterator);
				PrintTuple(clauseActors);
				PrintChar('\n');
				DictionaryIteratorNext(&dictIterator);
			}
			DictionaryIteratorEnd(&dictIterator);
		}
		RelationBTreeIteratorNext(&iterator);
	}
	RelationBTreeIteratorEnd(&iterator);
	FreeTuple(queryTuple);
	IFactRelease(queryTerm);

	DictionaryRemoveClause(rule);
	IFactRelease(rule);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	SetupServiceLibrary();

	ExecuteTest(testDispatchToService);
	ExecuteTest(testDispatchToRule);

	TeardownServiceLibrary();
	KernelShutdown();

	TestSummary();
}
