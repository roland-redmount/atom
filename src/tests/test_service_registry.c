
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


#define EXAMPLE_FORM_ARITY	4

struct {
	Atom form;		// a term form
	byte atomTypes[EXAMPLE_FORM_ARITY];
	RelationTable const * table;
} fixture;

// Services the kernel registers, which every count here is relative to
static size32 nCoreServices;


static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Formula * formula = CStringToTerm("foo 0 bar 0 bar 0 baz 0");
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


// A machine operator has to be given a provider, though nothing here evaluates one
static MachineProvider dummyProvider = {
	.setupContext = 0,
	.call = 0,
	.finalizeContext = 0,
	.finalizeOperator = 0
};


static Operator * createDummyMachineOperator(void)
{
	return CreateMachineOperator(
		EXAMPLE_FORM_ARITY, (index8[]) {0, 1, 2, 3}, &dummyProvider, 0, 0);
}


/**
 * Register a computed relation of the fixture form, distinguished from the others by its
 * column types, so that a service can be registered against it.
 */
static RelationTable const * createComputedRelation(byte const atomTypes[])
{
	RelationTable const * relation = CreateRelationTable(
		0, fixture.form, EXAMPLE_FORM_ARITY, atomTypes, 0);
	RelationRegistryAdd(relation);
	return relation;
}


/**
 * Register a compiled service built on the given operator, against a computed relation
 * of its own, and return the operator evaluating it.
 */
static Operator * addCompiledService(RelationTable const * relation, Operator * childOperator)
{
	byte parameterIO[EXAMPLE_FORM_ARITY] = {
		PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT, PARAMETER_OUT};
	Operator * op = CreatePermuteOperator(
		EXAMPLE_FORM_ARITY, 0, 0, 0, (index8[]) {0, 1, 2, 3}, childOperator);
	ServiceRegistryAdd(relation, parameterIO, op, SERVICE_COMPILED);
	ReleaseOperator(op);
	return op;
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
	Operator * op = createDummyMachineOperator();
	byte parameterIO[EXAMPLE_FORM_ARITY] = {PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT, PARAMETER_OUT};
	ASSERT_INT32_EQUAL(op->referenceCount, 1)
	ServiceRegistryAdd(fixture.table, parameterIO, op, SERVICE_PRIMITIVE);
	ASSERT_INT32_EQUAL(op->referenceCount, 2)

	ASSERT_PTR_EQUAL(
		ServiceRegistryFind(fixture.table, parameterIO),
		op
	);

	// Remove the service
	ServiceRegistryRemove(fixture.table, op);
	ASSERT_INT32_EQUAL(op->referenceCount, 1)
	ReleaseOperator(op);

	teardownFixture();
}


/**
 * Test that compiled services are removed when their dependencies are removed.
 */
void testInvalidateDependentServices(void)
{
	setupFixture();
	byte parameterIO[EXAMPLE_FORM_ARITY] = {
		PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT, PARAMETER_OUT};

	// Create dummy machine service
	Operator * machineOperator = createDummyMachineOperator();
	ServiceRegistryAdd(fixture.table, parameterIO, machineOperator, SERVICE_PRIMITIVE);

	// Hand-build a "compiled" service that depends on the machine service
	byte firstTypes[EXAMPLE_FORM_ARITY] = {AT_INT, AT_INT, AT_INT, AT_UINT};
	RelationTable const * firstRelation = createComputedRelation(firstTypes);
	Operator * firstOperator = addCompiledService(firstRelation, machineOperator);

	// A second "compiled" service that depends on the first one
	byte secondTypes[EXAMPLE_FORM_ARITY] = {AT_INT, AT_INT, AT_UINT, AT_UINT};
	RelationTable const * secondRelation = createComputedRelation(secondTypes);
	addCompiledService(secondRelation, firstOperator);

	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 2)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nCoreServices + 3)

	// Removing the machine service should remove both dependent services
	ServiceRegistryRemove(fixture.table, machineOperator);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nCoreServices)
	ASSERT_NULL(RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, firstTypes))
	ASSERT_NULL(RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, secondTypes))

	ASSERT_INT32_EQUAL(machineOperator->referenceCount, 1)
	ReleaseOperator(machineOperator);
	teardownFixture();
}


/**
 * Registering a primitive service (SERVICE_PRIMITIVE) gives a query of its term form
 * one more relation to match, so compiled services depending on this form must be invalidated.
 */
void testInvalidateOnPrimitiveService(void)
{
	setupFixture();
	byte parameterIO[EXAMPLE_FORM_ARITY] = {
		PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT, PARAMETER_OUT};

	// Register a dummy machine service
	Operator * machineOperator = createDummyMachineOperator();
	ServiceRegistryAdd(fixture.table, parameterIO, machineOperator, SERVICE_PRIMITIVE);

	// Create a "compiled" relation depending on the machine service
	byte compiledTypes[EXAMPLE_FORM_ARITY] = {AT_INT, AT_INT, AT_INT, AT_UINT};
	RelationTable const * compiledRelation = createComputedRelation(compiledTypes);
	addCompiledService(compiledRelation, machineOperator);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)

	// Create a second relation of the fixture form, with distinct atom types,
	// and associated primitive services
	byte storedTypes[EXAMPLE_FORM_ARITY] = {AT_UINT, AT_UINT, AT_UINT, AT_UINT};
	RelationTable const * storedRelation = CreateRelationBTreeWithServices(
		fixture.form, EXAMPLE_FORM_ARITY, storedTypes, (index8[]) {0, 1, 2, 3});
	// The above compiled service should now be invalidated
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_NULL(RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, compiledTypes))

	ServiceRegistryRemoveAll(storedRelation);
	RelationRegistryRemove(storedRelation);
	ServiceRegistryRemove(fixture.table, machineOperator);
	ReleaseOperator(machineOperator);
	teardownFixture();
}


/**
 * Test that two a compiled services sharing the same operator as another service
 * is invalidated when the operator is removed.
 * See removeOperatorServices() in ServiceRegistry.c for details.
 */
void testInvalidateSharedOperator(void)
{
	setupFixture();
	byte parameterIO[EXAMPLE_FORM_ARITY] = {
		PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT, PARAMETER_OUT};

	// Register a "dummy" machine service
	Operator * machineOperator = createDummyMachineOperator();
	ServiceRegistryAdd(fixture.table, parameterIO, machineOperator, SERVICE_PRIMITIVE);

	// Register a second service using the same machine operator
	byte compiledTypes[EXAMPLE_FORM_ARITY] = {AT_INT, AT_INT, AT_INT, AT_UINT};
	RelationTable const * compiledRelation = createComputedRelation(compiledTypes);
	ServiceRegistryAdd(compiledRelation, parameterIO, machineOperator, SERVICE_COMPILED);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)

	// Removing the machine service invalidates the compiled service
	ServiceRegistryRemove(fixture.table, machineOperator);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_NULL(RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, compiledTypes))

	ASSERT_INT32_EQUAL(machineOperator->referenceCount, 1)
	ReleaseOperator(machineOperator);
	teardownFixture();
}


int main(void)
{
	KernelInitialize();
	nCoreServices = ServiceRegistryCount();

	ExecuteTest(testAddRemoveService);
	ExecuteTest(testInvalidateDependentServices);
	ExecuteTest(testInvalidateOnPrimitiveService);
	ExecuteTest(testInvalidateSharedOperator);

	KernelShutdown();

	TestSummary();
}
