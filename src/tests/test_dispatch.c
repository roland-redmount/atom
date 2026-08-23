
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TermForm.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "lang/formula.h"
#include "library/MachineService.h"
#include "library/math.h"
#include "parser/TermBuilder.h"
#include "testing/fixtures.h"
#include "kernel/Parameter.h"
#include "lang/name.h"
#include "storage/RelationBTree.h"
#include "testing/testing.h"


/**
 * Test dispatching a query to the math service (+ + =)
 */
void testDispatchToService(void)
{
	Service service;
	Atom query;
	
	// this query matches with the identity permutation
	query = CStringToTerm("+ 3 + 4 = _");
	index8 permutation[3];
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ASSERT_UINT32_EQUAL(service.op->type, OPERATOR_MACHINE)
	ReleaseFormula(query);

	// one the following two queries requires form permutation to match
	query = CStringToTerm("+ 3 + _ = 7");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ReleaseFormula(query);

	query = CStringToTerm("+ _ + 3 = 7");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ReleaseFormula(query);
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
	// to the (list <ID position >INT element >LETTER) service all the same
	Atom query = CStringToTerm("list \"ab\" position x element x");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ReleaseFormula(query);

	// Distinct variables at those same positions match the same service
	query = CStringToTerm("list \"ab\" position p element e");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ReleaseFormula(query);
}


/**
 * A parameter occurring at several positions of a parameterized query does denote one atom,
 * which is the case of a rule body term with a repeated variable: the compiler dispatches
 * such a term and constrains the arguments providing it. Those positions can only match
 * service parameters of one type.
 */
void testDispatchRepeatedParameter(void)
{
	Atom query = CStringToTerm("list \"ab\" position p element e");
	size8 arity = FormulaGetActors(query)->nAtoms;
	Atom parameters[arity];
	GetQueryParameters(FormulaGetActors(query), parameters);

	Service service;
	index8 permutation[arity];
	ASSERT_TRUE(DispatchParameterizedQuery(
		FormulaGetForm(query), parameters, arity, &service, permutation, 0, 0, 0))

	// Give the position and the element one parameter, as a term (list s position p
	// element p) has. The service has an INT position and a LETTER element, so no atom
	// can be both.
	Atom repeatedParameter = {
		.parameter = {.number = 2, .io = PARAMETER_OUT, .atomType = 0}
	};
	Atom predicateForm = TermFormGetPredicateForm(FormulaGetForm(query));
	Atom positionRole = CreateNameFromCString("position");
	Atom elementRole = CreateNameFromCString("element");
	index8 positionIndex = PredicateRoleIndex(predicateForm, positionRole);
	index8 elementIndex = PredicateRoleIndex(predicateForm, elementRole);
	NameRelease(positionRole);
	NameRelease(elementRole);
	parameters[positionIndex] = repeatedParameter;
	parameters[elementIndex] = repeatedParameter;
	ASSERT_FALSE(DispatchParameterizedQuery(
		FormulaGetForm(query), parameters, arity, &service, permutation, 0, 0, 0))

	ReleaseFormula(query);
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
	TypeSignature typeSignature = CreateTypeSignature((byte[]) {AT_ID, AT_ID}, 2);
	Relation const * relation = CreateRelation(termForm, 2, typeSignature);
	RelationTable * table = CreateRelationTable(
		relation, &btreeTableProvider, (index8[]) {0, 1});
	ReleaseRelation(relation);
	Relation const * negatedRelation = CreateRelation(negatedTermForm, 2, typeSignature);
	RelationTable * negatedTable = CreateRelationTable(
		negatedRelation, &btreeTableProvider, (index8[]) {0, 1});
	ReleaseRelation(negatedRelation);
	ASSERT_PTR_NOT_EQUAL(table, negatedTable)
	ASSERT_PTR_EQUAL(RelationRegistryFind(termForm, 2, typeSignature), table->relation)
	ASSERT_PTR_EQUAL(RelationRegistryFind(negatedTermForm, 2, typeSignature), negatedTable->relation)
	// both tables report the predicate form the two term forms share
	ASSERT_DATA64_EQUAL(table->relation->predicateForm.hash, predicateForm.hash)
	ASSERT_DATA64_EQUAL(negatedTable->relation->predicateForm.hash, predicateForm.hash)

	// A negated query dispatches, and reaches the negated relation rather than the other
	Service service;
	index8 permutation[2];
	Atom query = CStringToTerm("! even x odd y");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ASSERT_PTR_EQUAL(service.relation, negatedTable->relation)
	ReleaseFormula(query);

	query = CStringToTerm("even x odd y");
	ASSERT_TRUE(DispatchQueryFormula(query, &service, permutation))
	ASSERT_PTR_EQUAL(service.relation, table->relation)
	ReleaseFormula(query);

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
	Relation const * idRelation = CreateRelation(
		termForm, 2, CreateTypeSignature((byte[]) {AT_ID, AT_ID}, 2));
	RelationTable * idTable = CreateRelationTable(
		idRelation, &btreeTableProvider, (index8[]) {0, 1});
	ReleaseRelation(idRelation);
	Relation const * intRelation = CreateRelation(
		termForm, 2, CreateTypeSignature((byte[]) {AT_ID, AT_INT}, 2));
	RelationTable * intTable = CreateRelationTable(
		intRelation, &btreeTableProvider, (index8[]) {0, 1});
	ReleaseRelation(intRelation);

	// Only the service with two output parameters matches, so each table contributes
	// one match
	Atom query = CStringToTerm("first x second y");
	Atom parameters[2];
	GetQueryParameters(FormulaGetActors(query), parameters);
	index8 permutation[2];
	DispatchIterator iterator;
	DispatchIterate(FormulaGetForm(query), parameters, 2, permutation, &iterator);

	// A match is named by the column types of the relation it reads, and excluding a name
	// asks for the other match. This is what the compiler enumerates a choice point by;
	// see compiler.c
	TypeSignature excludedTypes[2];
	bool foundIdRelation = false;
	bool foundIntRelation = false;

	size8 nMatches = 0;
	while(DispatchIteratorNext(&iterator)) {
		// The match the iterator is at is the one dispatch returns when every match before
		// it is excluded, whatever order the two are enumerated in
		Service excludeService;
		index8 excludePermutation[2];
		bool hasNextMatch;
		ASSERT_TRUE(DispatchParameterizedQuery(
			FormulaGetForm(query), parameters, 2, &excludeService, excludePermutation,
			excludedTypes, nMatches, &hasNextMatch))
		Service const * service = DispatchIteratorPeekService(&iterator);
		ASSERT_PTR_EQUAL(service->relation, excludeService.relation)
		ASSERT_PTR_EQUAL(service->op, excludeService.op)
		for(index8 i = 0; i < 2; i++)
			ASSERT_UINT32_EQUAL(permutation[i], excludePermutation[i])

		// The relation a match reads is what names it, so excluding its signature is what
		// asks for the other match
		excludedTypes[nMatches] = excludeService.relation->typeSignature;
		ASSERT_UINT32_EQUAL(excludedTypes[nMatches].atomTypes[0], AT_ID)
		if(excludedTypes[nMatches].atomTypes[1] == AT_ID)
			foundIdRelation = true;
		else if(excludedTypes[nMatches].atomTypes[1] == AT_INT)
			foundIntRelation = true;

		nMatches++;
		ASSERT_TRUE(hasNextMatch == (nMatches < 2))
	}
	ASSERT_UINT32_EQUAL(nMatches, 2)
	// Both relations are reached, whichever order the registry yields them in
	ASSERT_TRUE(foundIdRelation)
	ASSERT_TRUE(foundIntRelation)
	DispatchIteratorEnd(&iterator);

	// With every match excluded there is nothing left to return
	Service exhaustedService;
	index8 exhaustedPermutation[2];
	ASSERT_FALSE(DispatchParameterizedQuery(
		FormulaGetForm(query), parameters, 2, &exhaustedService, exhaustedPermutation,
		excludedTypes, 2, 0))

	// An iterator abandoned before the last match is released just as well
	DispatchIterate(FormulaGetForm(query), parameters, 2, permutation, &iterator);
	ASSERT_TRUE(DispatchIteratorNext(&iterator))
	DispatchIteratorEnd(&iterator);
	ReleaseFormula(query);

	// A query for a form with no relation yields no match at all
	Atom unknownQuery = CStringToTerm("nowhere x nothing y");
	Atom unknownParameters[2];
	GetQueryParameters(FormulaGetActors(unknownQuery), unknownParameters);
	DispatchIterate(FormulaGetForm(unknownQuery), unknownParameters, 2, permutation, &iterator);
	ASSERT_FALSE(DispatchIteratorNext(&iterator))
	DispatchIteratorEnd(&iterator);
	ReleaseFormula(unknownQuery);

	DropRelationTable(intTable);
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
