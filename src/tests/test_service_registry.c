
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTableRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "storage/RelationBTree.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


#define EXAMPLE_FORM_ARITY	4

struct {
	Atom form;		// a term form
	byte atomTypes[EXAMPLE_FORM_ARITY];
	// The relation the services under test are registered against. A computed relation:
	// a service needs no tuple storage, which is the point of registering against a
	// relation rather than against a table.
	Relation const * relation;
} fixture;

// Services the kernel registers, which every count here is relative to
static size32 nCoreServices;

static byte const exampleParameterIO[EXAMPLE_FORM_ARITY] = {
	PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT, PARAMETER_OUT};


static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Formula * formula = CStringToTerm("foo 0 bar 0 bar 0 baz 0");
	fixture.form = formula->form;
	SetMemory(fixture.atomTypes, EXAMPLE_FORM_ARITY, AT_INT);
	IFactAcquire(fixture.form);
	FreeFormula(formula);

	// services are registered per relation, so we need one to test with
	fixture.relation = CreateRelation(fixture.form, EXAMPLE_FORM_ARITY, fixture.atomTypes);
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
 * column types, so that a service can be registered against it. Returns holding the
 * creation reference; see CreateRelation().
 */
static Relation const * createComputedRelation(byte const atomTypes[])
{
	return CreateRelation(fixture.form, EXAMPLE_FORM_ARITY, atomTypes);
}


/**
 * Register a compiled service built on the given operator, against a computed relation
 * of its own, and return the operator evaluating it.
 */
static Operator * addCompiledService(Relation const * relation, Operator * childOperator)
{
	Operator * op = CreatePermuteOperator(
		EXAMPLE_FORM_ARITY, 0, 0, 0, (index8[]) {0, 1, 2, 3}, childOperator);
	ServiceRegistryAdd(relation, exampleParameterIO, op, SERVICE_COMPILED);
	ReleaseOperator(op);
	return op;
}


static void teardownFixture(void)
{
	ReleaseRelation(fixture.relation);
	IFactRelease(fixture.form);
}


void testAddRemoveService(void)
{
	setupFixture();

	// Add a dummy service to the relation
	Operator * op = createDummyMachineOperator();
	ASSERT_INT32_EQUAL(op->referenceCount, 1)
	ServiceRegistryAdd(fixture.relation, exampleParameterIO, op, SERVICE_PRIMITIVE);
	ASSERT_INT32_EQUAL(op->referenceCount, 2)

	ASSERT_PTR_EQUAL(
		ServiceRegistryFind(fixture.relation, exampleParameterIO),
		op
	);

	// Remove the service
	ServiceRegistryRemove(fixture.relation, op);
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

	// Create dummy machine service
	Operator * machineOperator = createDummyMachineOperator();
	ServiceRegistryAdd(fixture.relation, exampleParameterIO, machineOperator, SERVICE_PRIMITIVE);

	// Hand-build a "compiled" service that depends on the machine service
	byte firstTypes[EXAMPLE_FORM_ARITY] = {AT_INT, AT_INT, AT_INT, AT_UINT};
	Relation const * firstRelation = createComputedRelation(firstTypes);
	Operator * firstOperator = addCompiledService(firstRelation, machineOperator);
	ReleaseRelation(firstRelation);

	// A second "compiled" service that depends on the first one
	byte secondTypes[EXAMPLE_FORM_ARITY] = {AT_INT, AT_INT, AT_UINT, AT_UINT};
	Relation const * secondRelation = createComputedRelation(secondTypes);
	addCompiledService(secondRelation, firstOperator);
	ReleaseRelation(secondRelation);

	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 2)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nCoreServices + 3)

	// Removing the machine service should remove both dependent services
	ServiceRegistryRemove(fixture.relation, machineOperator);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nCoreServices)
	// A computed relation nothing names any longer goes with its last service
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

	// Register a dummy machine service
	Operator * machineOperator = createDummyMachineOperator();
	ServiceRegistryAdd(fixture.relation, exampleParameterIO, machineOperator, SERVICE_PRIMITIVE);

	// Create a "compiled" relation depending on the machine service
	byte compiledTypes[EXAMPLE_FORM_ARITY] = {AT_INT, AT_INT, AT_INT, AT_UINT};
	Relation const * compiledRelation = createComputedRelation(compiledTypes);
	addCompiledService(compiledRelation, machineOperator);
	ReleaseRelation(compiledRelation);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)

	// Create a second relation of the fixture form, with distinct atom types,
	// and associated primitive services
	byte storedTypes[EXAMPLE_FORM_ARITY] = {AT_UINT, AT_UINT, AT_UINT, AT_UINT};
	Relation const * storedRelation = CreateRelation(fixture.form, EXAMPLE_FORM_ARITY, storedTypes);
	RelationTable * storedTable = CreateRelationTable(
		storedRelation, &btreeTableProvider, (index8[]) {0, 1, 2, 3});
	ReleaseRelation(storedRelation);
	// The above compiled service should now be invalidated
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_NULL(RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, compiledTypes))

	DropRelationTable(storedTable);
	ServiceRegistryRemove(fixture.relation, machineOperator);
	ReleaseOperator(machineOperator);
	teardownFixture();
}


/**
 * A compiled service sharing its operator with another service is invalidated when that
 * service is removed. This is a rule about the knowledge base rather than about memory:
 * the two are one operator, and so the same tuples under two signatures.
 * See removeOperatorServices() in ServiceRegistry.c for details.
 */
void testInvalidateSharedOperator(void)
{
	setupFixture();

	// Register a "dummy" machine service
	Operator * machineOperator = createDummyMachineOperator();
	ServiceRegistryAdd(fixture.relation, exampleParameterIO, machineOperator, SERVICE_PRIMITIVE);

	// Register a second service using the same machine operator
	byte compiledTypes[EXAMPLE_FORM_ARITY] = {AT_INT, AT_INT, AT_INT, AT_UINT};
	Relation const * compiledRelation = createComputedRelation(compiledTypes);
	ServiceRegistryAdd(compiledRelation, exampleParameterIO, machineOperator, SERVICE_COMPILED);
	ReleaseRelation(compiledRelation);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)

	// Removing the machine service invalidates the compiled service
	ServiceRegistryRemove(fixture.relation, machineOperator);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_NULL(RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, compiledTypes))

	ASSERT_INT32_EQUAL(machineOperator->referenceCount, 1)
	ReleaseOperator(machineOperator);
	teardownFixture();
}


/**
 * Register a compiled service of the fixture relation evaluated by an operator of the
 * given stored table, which is what a rule that merely renames roles compiles to; see
 * compileTerm() in compiler.c. Returns that operator, which the two services now share.
 */
static Operator * shareTableOperator(RelationTable * table)
{
	Operator * op = ServiceRegistryFind(table->relation, exampleParameterIO);
	ASSERT_NOT_NULL(op)
	ServiceRegistryAdd(fixture.relation, exampleParameterIO, op, SERVICE_COMPILED);
	return op;
}


/**
 * Dropping a table whose operator a compiled service shares removes that service, and
 * leaves the storage alive until the shared operator is released.
 *
 * The two are independent: the compiled service goes because the relation it reads has
 * left the knowledge base, while the storage survives because an operator still points at
 * it. Before relation tables were reference counted, the removal was the only thing
 * standing between this case and a dangling pointer.
 */
void testDropTableWithSharedOperator(void)
{
	setupFixture();
	byte storedTypes[EXAMPLE_FORM_ARITY] = {AT_UINT, AT_UINT, AT_UINT, AT_UINT};
	Relation const * relation = CreateRelation(fixture.form, EXAMPLE_FORM_ARITY, storedTypes);
	RelationTable * table = CreateRelationTable(
		relation, &btreeTableProvider, (index8[]) {0, 1, 2, 3});
	ReleaseRelation(relation);

	Operator * sharedOperator = shareTableOperator(table);
	AcquireOperator(sharedOperator);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)

	// The table holds the creation reference, and one per registered service operator
	ASSERT_UINT32_EQUAL(table->referenceCount, 1 + EXAMPLE_FORM_ARITY + 1)

	DropRelationTable(table);
	// The compiled service went with the services of the table it shared an operator with
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_NULL(RelationTableRegistryFind(table->relation))
	// but the storage is still there, as this test still holds the shared operator
	ASSERT_UINT32_EQUAL(table->referenceCount, 1)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 0)

	// Releasing the last operator reading the table deallocates it
	ReleaseOperator(sharedOperator);
	teardownFixture();
}


/**
 * The same in the opposite order: releasing the shared operator first and dropping the
 * table afterwards. Neither order is required, which is what reference counting the table
 * buys over removing services in a fixed sequence.
 */
void testDropTableAfterSharedOperator(void)
{
	setupFixture();
	byte storedTypes[EXAMPLE_FORM_ARITY] = {AT_UINT, AT_UINT, AT_UINT, AT_UINT};
	Relation const * relation = CreateRelation(fixture.form, EXAMPLE_FORM_ARITY, storedTypes);
	RelationTable * table = CreateRelationTable(
		relation, &btreeTableProvider, (index8[]) {0, 1, 2, 3});
	ReleaseRelation(relation);

	Operator * sharedOperator = shareTableOperator(table);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)

	// Remove the compiled service first, so the operator is the stored service's alone
	ServiceRegistryRemove(fixture.relation, sharedOperator);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_UINT32_EQUAL(table->referenceCount, 1 + EXAMPLE_FORM_ARITY + 1)

	DropRelationTable(table);
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nCoreServices)
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
	ExecuteTest(testDropTableWithSharedOperator);
	ExecuteTest(testDropTableAfterSharedOperator);

	KernelShutdown();

	TestSummary();
}
