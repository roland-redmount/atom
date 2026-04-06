
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"


struct {
	Atom signature;		// a form
} fixture;

static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Atom formula = CStringToPredicate("foo 0 bar 0 bar 0 baz 0");
	fixture.signature = FormulaGetForm(formula);
	AcquireAtom(fixture.signature);
	IFactRelease(formula);
}

static void teardownFixture(void)
{
	ReleaseAtom(fixture.signature);
}


void testAddDropTable(void)
{
	setupFixture();
	size32 nTablesInitial = RegistryNServices();

	BTree * createdTable = CreateRelationBTree(4);
	RegistryAddBTreeService(fixture.signature, createdTable);
	ASSERT_UINT32_EQUAL(RegistryNServices(), nTablesInitial + 1)
	ASSERT_UINT32_EQUAL(RelationBTreeNColumns(createdTable), 4)

	Service service = RegistryFindService(fixture.signature);
	ASSERT(service.type == SERVICE_BTREE)
	BTree * foundTable = service.service.tree;
	ASSERT_PTR_NOT_EQUAL(foundTable, 0)
	ASSERT_PTR_EQUAL(foundTable, createdTable)

	RegistryRemoveService(fixture.signature);
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
