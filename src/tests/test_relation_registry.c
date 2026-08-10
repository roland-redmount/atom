
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "lang/Formula.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


#define EXAMPLE_FORM_ARITY	4

struct {
	Atom form;		// a term form
	byte atomTypes[EXAMPLE_FORM_ARITY];
} fixture;


static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Formula * formula = CStringToTerm("foo 0 bar 0 bar 0 baz 0");
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


/**
 * The core term form (multiset element multiple) has two relation tables,
 * one for NAME elements and one for ID elements, so it is a good case
 * for iterating over the tables of a single form.
 */
void testIterateRelationTables(void)
{
	Atom form = GetCoreTermForm(FORM_MULTISET_ELEMENT_MULTIPLE);
	RelationTable const * multisetName = GetCoreRelationTable(RELATION_MULTISET_NAME);
	RelationTable const * multisetId = GetCoreRelationTable(RELATION_MULTISET_ID);

	bool foundName = false;
	bool foundId = false;
	size32 nTables = 0;

	RelationIterator iterator;
	RelationRegistryIterate(form, &iterator);
	while(RelationIteratorNext(&iterator)) {
		RelationTable const * table = RelationIteratorGet(&iterator);
		// every table yielded must belong to the form we asked for
		ASSERT_DATA64_EQUAL(table->form.hash, form.hash)
		if(table == multisetName)
			foundName = true;
		if(table == multisetId)
			foundId = true;
		nTables++;
	}
	RelationIteratorEnd(&iterator);

	ASSERT_TRUE(foundName)
	ASSERT_TRUE(foundId)
	ASSERT_UINT32_EQUAL(nTables, 2)

	// a form with a single table
	form = GetCoreTermForm(FORM_LIST_LENGTH);
	nTables = 0;
	RelationRegistryIterate(form, &iterator);
	while(RelationIteratorNext(&iterator)) {
		ASSERT_PTR_EQUAL(RelationIteratorGet(&iterator), GetCoreRelationTable(RELATION_LIST_LENGTH))
		nTables++;
	}
	RelationIteratorEnd(&iterator);
	ASSERT_UINT32_EQUAL(nTables, 1)

	// an unregistered form yields nothing
	setupFixture();
	RelationRegistryIterate(fixture.form, &iterator);
	ASSERT_FALSE(RelationIteratorNext(&iterator))
	RelationIteratorEnd(&iterator);
	teardownFixture();
}


int main(void)
{
	KernelInitialize();

	ExecuteTest(testAddRemoveRelationTable);
	ExecuteTest(testIterateRelationTables);

	KernelShutdown();

	TestSummary();
}
