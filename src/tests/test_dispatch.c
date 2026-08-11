
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "lang/name.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "lang/Formula.h"
#include "library/MachineService.h"
#include "library/math.h"
#include "parser/TermBuilder.h"
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
	Atom roles[2] = {
		CreateNameFromCString("even"),
		CreateNameFromCString("odd")
	};
	Atom predicateForm = CreatePredicateForm(roles, 2);
	Atom termForm = CreateTermForm(predicateForm, true);
	Atom negatedTermForm = CreateTermForm(predicateForm, false);
	for(index8 i = 0; i < 2; i++)
		NameRelease(roles[i]);

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
	IFactRelease(predicateForm);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testDispatchToService);
	ExecuteTest(testDispatchRepeatedVariable);
	ExecuteTest(testDispatchNegatedTerm);

	FreeMachineServices();
	KernelShutdown();
	TestSummary();
}
