
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"


#define EXAMPLE_FORM_ARITY	4

struct {
	Atom form;		// a form
	byte atomTypes[EXAMPLE_FORM_ARITY];
	RelationTable const * table;
} fixture;


static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Formula * formula = CStringToPredicate("foo 0 bar 0 bar 0 baz 0");
	fixture.form = formula->form;
	SetMemory(fixture.atomTypes, EXAMPLE_FORM_ARITY, AT_INT);
	IFactAcquire(fixture.form);
	FreeFormula(formula);

	// services are registered per relation table, so we need one to test with
	fixture.table = CreateRelationTable(
		&btreeTableProvider, fixture.form, EXAMPLE_FORM_ARITY,
		fixture.atomTypes, 0
	);
	RelationRegistryAdd(fixture.table);
}


static void teardownFixture(void)
{
	RelationRegistryRemove(fixture.table);
	IFactRelease(fixture.form);
}


void testAddRemoveService(void)
{
	setupFixture();

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
	ServiceRegistryAdd(fixture.table, parameterIO, service);
	ASSERT_INT32_EQUAL(service->referenceCount, 2)

	ASSERT_PTR_EQUAL(
		ServiceRegistryFind(fixture.table, parameterIO),
		service
	);

	// Remove the service
	ServiceRegistryRemove(fixture.table, service);
	ASSERT_INT32_EQUAL(service->referenceCount, 1)
	ReleaseService(service);

	teardownFixture();
}


int main(void)
{
	KernelInitialize();

	ExecuteTest(testAddRemoveService);

	KernelShutdown();

	TestSummary();
}
