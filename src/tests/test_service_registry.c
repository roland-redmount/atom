
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/Parameter.h"
#include "kernel/RelationBTree.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "lang/ConjunctionForm.h"
#include "lang/Variable.h"
#include "memory/allocator.h"
#include "parser/PredicateBuilder.h"
#include "parser/ConjunctionBuilder.h"
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


void testAddRemoveRelation(void)
{
	setupFixture();
	size32 nTablesInitial = RegistryNRelations();

	RelationTable const * createdTable = CreateRelationTable(
		&btreeTableProvider, fixture.form, EXAMPLE_FORM_ARITY,
		fixture.atomTypes, 0
	);
	ASSERT_UINT32_EQUAL(createdTable->nColumns, EXAMPLE_FORM_ARITY)

	// Add relation table to the registry
	RegistryAddRelationTable(createdTable);
	ASSERT_UINT32_EQUAL(RegistryNRelations(), nTablesInitial + 1)

	ASSERT_PTR_EQUAL(
		FindRelationTable(fixture.form, EXAMPLE_FORM_ARITY, fixture.atomTypes),
		createdTable
	)

	// Add a dummy service to the relation table
	MachineServiceProvider dummyProvider = {
		.setupContext = 0,
		.call = 0,
		.finalizeContext = 0,
		.finalizeService = 0,
		.contextSize = 0
	};

	Service * service = CreateMachineService(EXAMPLE_FORM_ARITY, &dummyProvider, 0);
	byte parameterIO[EXAMPLE_FORM_ARITY] = {PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT, PARAMETER_OUT};
	ASSERT_INT32_EQUAL(service->referenceCount, 1)
	RelationAddService(createdTable, parameterIO, service);
	ASSERT_INT32_EQUAL(service->referenceCount, 2)

	ASSERT_PTR_EQUAL(
		RegistryFindService(createdTable, parameterIO),
		service
	);

	// Remove the service
	RelationRemoveService(createdTable, service);
	ASSERT_INT32_EQUAL(service->referenceCount, 1)
	ReleaseService(service);

	// Remove the table
	RegistryRemoveRelationTable(createdTable);
	ASSERT_UINT32_EQUAL(RegistryNRelations(), nTablesInitial)

	ASSERT_NULL(FindRelationTable(fixture.form, EXAMPLE_FORM_ARITY, fixture.atomTypes));
	
	teardownFixture();
}


int main(void)
{
	KernelInitialize();

	ExecuteTest(testAddRemoveRelation);

	KernelShutdown();

	TestSummary();
}
