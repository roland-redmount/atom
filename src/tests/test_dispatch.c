
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
#include "kernel/Parameter.h"
#include "lang/name.h"
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
 * A query is dispatched by its type, in which every actor is a parameter of its own, so a
 * variable occurring at several positions matches as if the occurrences were unrelated.
 * The tuples such a service yields are more than the query asked for, and the caller
 * filters them; see MixedTypeRelation.h.
 */
void testDispatchRepeatedVariable(void)
{
	Service service;
	index8 permutation[3];

	// A position is never a letter, so no atom satisfies this query, and it dispatches
	// to the (list <ID position >UINT element >LETTER) service all the same
	Formula * query = CStringToTerm("list \"ab\" position _x element _x");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	FreeFormula(query);

	// Distinct variables at those same positions match the same service
	query = CStringToTerm("list \"ab\" position _p element _e");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	FreeFormula(query);
}


/**
 * A parameter occurring at several positions of a generalized query does denote one atom,
 * which is the case of a rule body term with a repeated variable: the compiler dispatches
 * such a term and constrains the arguments providing it. Those positions can only match
 * service parameters of one type.
 */
void testDispatchRepeatedParameter(void)
{
	Formula * query = CStringToTerm("list \"ab\" position _p element _e");
	size8 arity = query->actors->nAtoms;
	Atom parameters[arity];
	GetQueryParameters(query->actors, parameters);

	Service service;
	index8 permutation[arity];
	ASSERT_TRUE(DispatchGeneralizedQuery(
		query->form, parameters, arity, &service, permutation, 0, 0))

	// Give the position and the element one parameter, as a term (list s position p
	// element p) has. The service has a UINT position and a LETTER element, so no atom
	// can be both.
	Atom repeatedParameter = {
		.parameter = {.number = 2, .io = PARAMETER_OUT, .atomType = 0}
	};
	Atom predicateForm = TermFormGetPredicateForm(query->form);
	Atom positionRole = CreateNameFromCString("position");
	Atom elementRole = CreateNameFromCString("element");
	index8 positionIndex = PredicateRoleIndex(predicateForm, positionRole);
	index8 elementIndex = PredicateRoleIndex(predicateForm, elementRole);
	NameRelease(positionRole);
	NameRelease(elementRole);
	parameters[positionIndex] = repeatedParameter;
	parameters[elementIndex] = repeatedParameter;
	ASSERT_FALSE(DispatchGeneralizedQuery(
		query->form, parameters, arity, &service, permutation, 0, 0))

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
	RelationTable * table = CreateRelationBTreeWithServices(
		termForm, 2, atomTypes, (index8[]) {0, 1});
	RelationTable * negatedTable = CreateRelationBTreeWithServices(
		negatedTermForm, 2, atomTypes, (index8[]) {0, 1});
	ASSERT_PTR_NOT_EQUAL(table, negatedTable)
	ASSERT_PTR_EQUAL(RelationRegistryFind(termForm, 2, atomTypes), table->relation)
	ASSERT_PTR_EQUAL(RelationRegistryFind(negatedTermForm, 2, atomTypes), negatedTable->relation)
	// both tables report the predicate form the two term forms share
	ASSERT_DATA64_EQUAL(table->relation->predicateForm.hash, predicateForm.hash)
	ASSERT_DATA64_EQUAL(negatedTable->relation->predicateForm.hash, predicateForm.hash)

	// A negated query dispatches, and reaches the negated relation rather than the other
	Service service;
	index8 permutation[2];
	Formula * query = CStringToTerm("! even _x odd _y");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ASSERT_PTR_EQUAL(service.relation, negatedTable->relation)
	FreeFormula(query);

	query = CStringToTerm("even _x odd _y");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ASSERT_PTR_EQUAL(service.relation, table->relation)
	FreeFormula(query);

	DropRelationTable(negatedTable);
	DropRelationTable(table);
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
	RelationTable * idTable = CreateRelationBTreeWithServices(
		termForm, 2, (byte[]) {AT_ID, AT_ID}, (index8[]) {0, 1});
	RelationTable * uintTable = CreateRelationBTreeWithServices(
		termForm, 2, (byte[]) {AT_ID, AT_UINT}, (index8[]) {0, 1});

	// Only the service with two output parameters matches, so each table contributes
	// one match
	Formula * query = CStringToTerm("first _x second _y");
	Atom parameters[2];
	GetQueryParameters(query->actors, parameters);
	index8 permutation[2];
	DispatchIterator iterator;
	DispatchIterate(query->form, parameters, 2, permutation, &iterator);

	size8 nMatches = 0;
	while(DispatchIteratorNext(&iterator)) {
		// Match number k is the same returned by DispatchGeneralizedQuery() with nSkip = k
		Service skipService;
		index8 skipPermutation[2];
		bool hasNextMatch;
		ASSERT_TRUE(DispatchGeneralizedQuery(
			query->form, parameters, 2, &skipService, skipPermutation, nMatches, &hasNextMatch))
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
	DispatchIterate(query->form, parameters, 2, permutation, &iterator);
	ASSERT_TRUE(DispatchIteratorNext(&iterator))
	DispatchIteratorEnd(&iterator);
	FreeFormula(query);

	// A query for a form with no relation yields no match at all
	Formula * unknownQuery = CStringToTerm("nowhere _x nothing _y");
	Atom unknownParameters[2];
	GetQueryParameters(unknownQuery->actors, unknownParameters);
	DispatchIterate(unknownQuery->form, unknownParameters, 2, permutation, &iterator);
	ASSERT_FALSE(DispatchIteratorNext(&iterator))
	DispatchIteratorEnd(&iterator);
	FreeFormula(unknownQuery);

	DropRelationTable(uintTable);
	DropRelationTable(idTable);
	IFactRelease(termForm);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testDispatchToService);
	ExecuteTest(testDispatchRepeatedVariable);
	ExecuteTest(testDispatchRepeatedParameter);
	ExecuteTest(testDispatchNegatedTerm);
	ExecuteTest(testDispatchIterator);

	FreeMachineServices();
	KernelShutdown();
	TestSummary();
}
