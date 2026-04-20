
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"


struct {
	Atom form;		// a form
} fixture;

static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Atom formula = CStringToPredicate("foo 0 bar 0 bar 0 baz 0");
	fixture.form = FormulaGetForm(formula);
	IFactAcquire(fixture.form);
	IFactRelease(formula);
}

static void teardownFixture(void)
{
	IFactRelease(fixture.form);
}


void testAddDropTable(void)
{
	setupFixture();
	size32 nTablesInitial = RegistryNServices();

	BTree * createdTable = CreateRelationBTree(4);
	Atom service = RegistryAddBTreeService(fixture.form, createdTable);
	ASSERT_UINT32_EQUAL(RegistryNServices(), nTablesInitial + 1)
	ASSERT_UINT32_EQUAL(RelationBTreeNColumns(createdTable), 4)

	ServiceRecord record = RegistryFindBTreeService(fixture.form);
	ASSERT(record.type == SERVICE_BTREE)
	BTree * foundTable = record.provider.tree;
	ASSERT_PTR_NOT_EQUAL(foundTable, 0)
	ASSERT_PTR_EQUAL(foundTable, createdTable)

	RegistryRemoveService(service);
	ASSERT_UINT32_EQUAL(RegistryNServices(), nTablesInitial)
	
	teardownFixture();
}


int main(void)
{
	KernelInitialize();

	ExecuteTest(testAddDropTable);

	KernelShutdown();

	TestSummary();
}
