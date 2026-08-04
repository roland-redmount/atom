
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "lang/Formula.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"


#define EXAMPLE_FORM_ARITY	4

struct {
	Atom form;		// a form
	byte atomTypes[EXAMPLE_FORM_ARITY];
} fixture;


static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Formula * formula = CStringToPredicate("foo 0 bar 0 bar 0 baz 0");
	fixture.form = formula->form;
	SetMemory(fixture.atomTypes, EXAMPLE_FORM_ARITY, AT_INT);
	IFactAcquire(fixture.form);
	FreeFormula(formula);
}


static void teardownFixture(void)
{
	IFactRelease(fixture.form);
}


void testAddRemoveRelationTable(void)
{
	setupFixture();
	size32 nTablesInitial = RelationRegistryNTables();

	RelationTable const * createdTable = CreateRelationTable(
		&btreeTableProvider, fixture.form, EXAMPLE_FORM_ARITY,
		fixture.atomTypes, 0
	);
	ASSERT_UINT32_EQUAL(createdTable->nColumns, EXAMPLE_FORM_ARITY)

	// Add relation table to the registry
	RelationRegistryAdd(createdTable);
	ASSERT_UINT32_EQUAL(RelationRegistryNTables(), nTablesInitial + 1)

	ASSERT_PTR_EQUAL(
		RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, fixture.atomTypes),
		createdTable
	)

	// Remove the table
	RelationRegistryRemove(createdTable);
	ASSERT_UINT32_EQUAL(RelationRegistryNTables(), nTablesInitial)

	ASSERT_NULL(RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, fixture.atomTypes));

	teardownFixture();
}


int main(void)
{
	KernelInitialize();

	ExecuteTest(testAddRemoveRelationTable);

	KernelShutdown();

	TestSummary();
}
