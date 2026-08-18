
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TermForm.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "lang/Formula.h"
#include "library/MachineService.h"
#include "library/math.h"
#include "parser/TermBuilder.h"
#include "testing/fixtures.h"
#include "testing/testing.h"


/**
 * Test dispatching a query to the math service (+ + =)
 */
void testDispatchToService(void)
{
	Service service;
	Formula * query;
	
	// this query matches with the identity permutation
	query = CStringToTerm("+ 3 + 4 = _");
	index8 permutation[3];
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ASSERT_UINT32_EQUAL(service.op->type, OPERATOR_MACHINE)
	FreeFormula(query);

	// one the following two queries requires form permutation to match
	query = CStringToTerm("+ 3 + _ = 7");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	FreeFormula(query);

	query = CStringToTerm("+ _ + 3 = 7");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	FreeFormula(query);
}


/**
 * A variable occurring at several positions of a query denotes one atom, so it
 * can only match service parameters of the same type.
 */
void testDispatchRepeatedVariable(void)
{
	Service service;
	index8 permutation[3];
	Formula * query;

	// The service (list <ID position >UINT element >LETTER) has a position of a
	// different type than an element, so no atom can be both
	query = CStringToTerm("list \"ab\" position _x element _x");
	ASSERT_FALSE(DispatchQueryFormula(query, &service, permutation))
	FreeFormula(query);

	// Distinct variables at those same positions match as before
	query = CStringToTerm("list \"ab\" position _p element _e");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	FreeFormula(query);

	// Each occurence of the anonymous variable is a variable of its own
	query = CStringToTerm("list \"ab\" position _ element _");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	FreeFormula(query);
}


/**
 * A relation table is keyed by a term form, which carries a sign, so a predicate and its
 * negation have a relation table each. A query only reaches the table of its own sign.
 */
void testDispatchNegatedTerm(void)
{
	Atom termForm = CreateTermFormFromRoleNames((char const * []) {"even", "odd"}, 2, true);
	Atom predicateForm = TermFormGetPredicateForm(termForm);
	Atom negatedTermForm = CreateTermForm(predicateForm, false);

	// The two signs give two distinct relation tables
	byte atomTypes[2] = {AT_ID, AT_ID};
	RelationTable const * table = CreateRelationBTreeWithServices(
		termForm, 2, atomTypes, (index8[]) {0, 1});
	RelationTable const * negatedTable = CreateRelationBTreeWithServices(
		negatedTermForm, 2, atomTypes, (index8[]) {0, 1});
	ASSERT_PTR_NOT_EQUAL(table, negatedTable)
	ASSERT_PTR_EQUAL(RelationRegistryFind(termForm, 2, atomTypes), table)
	ASSERT_PTR_EQUAL(RelationRegistryFind(negatedTermForm, 2, atomTypes), negatedTable)
	// both tables report the predicate form the two term forms share
	ASSERT_DATA64_EQUAL(table->predicateForm.hash, predicateForm.hash)
	ASSERT_DATA64_EQUAL(negatedTable->predicateForm.hash, predicateForm.hash)

	// A negated query dispatches, and reaches the negated relation rather than the other
	Service service;
	index8 permutation[2];
	Formula * query = CStringToTerm("! even _x odd _y");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ASSERT_PTR_EQUAL(service.relation, negatedTable)
	FreeFormula(query);

	query = CStringToTerm("even _x odd _y");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ASSERT_PTR_EQUAL(service.relation, table)
	FreeFormula(query);

	ServiceRegistryRemoveAll(negatedTable);
	RelationRegistryRemove(negatedTable);
	ServiceRegistryRemoveAll(table);
	RelationRegistryRemove(table);
	IFactRelease(negatedTermForm);
	IFactRelease(termForm);
}


/**
 * Test iterating over all services matching a given query. Here, the query
 * (first x second y) matches one service for each of the two underyling relation tables.
 */
void testDispatchIterator(void)
{
	Atom termForm = CreateTermFormFromRoleNames(
		(char const * []) {"first", "second"}, 2, true);

	// Two relation tables for the term form, one per combination of column types
	RelationTable const * idTable = CreateRelationBTreeWithServices(
		termForm, 2, (byte[]) {AT_ID, AT_ID}, (index8[]) {0, 1});
	RelationTable const * uintTable = CreateRelationBTreeWithServices(
		termForm, 2, (byte[]) {AT_ID, AT_UINT}, (index8[]) {0, 1});

	// Only the service with two output parameters matches, so each table contributes
	// one match
	Formula * query = CStringToTerm("first _x second _y");
	index8 permutation[2];
	DispatchIterator iterator;
	DispatchQueryIterate(query->form, query->actors, permutation, &iterator);

	size8 nMatches = 0;
	while(DispatchIteratorNext(&iterator)) {
		// Match number k is the same returned by DispatchQueryAt() with nSkip = k
		Service skipService;
		index8 skipPermutation[2];
		bool hasNextMatch;
		ASSERT_TRUE(DispatchQueryAt(
			query->form, query->actors, &skipService, skipPermutation, nMatches, &hasNextMatch))
		Service const * service = DispatchIteratorPeekService(&iterator);
		ASSERT_PTR_EQUAL(service->relation, skipService.relation)
		ASSERT_PTR_EQUAL(service->op, skipService.op)
		for(index8 i = 0; i < 2; i++)
			ASSERT_UINT32_EQUAL(permutation[i], skipPermutation[i])

		nMatches++;
		ASSERT_TRUE(hasNextMatch == (nMatches < 2))
	}
	ASSERT_UINT32_EQUAL(nMatches, 2)
	DispatchIteratorEnd(&iterator);

	// An iterator abandoned before the last match is released just as well
	DispatchQueryIterate(query->form, query->actors, permutation, &iterator);
	ASSERT_TRUE(DispatchIteratorNext(&iterator))
	DispatchIteratorEnd(&iterator);
	FreeFormula(query);

	// A query for a form with no relation table yields no match at all
	Formula * unknownQuery = CStringToTerm("nowhere _x nothing _y");
	DispatchQueryIterate(unknownQuery->form, unknownQuery->actors, permutation, &iterator);
	ASSERT_FALSE(DispatchIteratorNext(&iterator))
	DispatchIteratorEnd(&iterator);
	FreeFormula(unknownQuery);

	ServiceRegistryRemoveAll(uintTable);
	RelationRegistryRemove(uintTable);
	ServiceRegistryRemoveAll(idTable);
	RelationRegistryRemove(idTable);
	IFactRelease(termForm);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testDispatchToService);
	ExecuteTest(testDispatchRepeatedVariable);
	ExecuteTest(testDispatchNegatedTerm);
	ExecuteTest(testDispatchIterator);

	FreeMachineServices();
	KernelShutdown();
	TestSummary();
}
