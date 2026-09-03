
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/formula.h"
#include "library/list.h"
#include "library/string.h"
#include "parser/TermBuilder.h"
#include "storage/RelationBTree.h"
#include "testing/testing.h"


#define EXAMPLE_FORM_ARITY	4

struct {
	Atom form;		// a term form
	TypeSignature typeSignature;
	// The relation the services under test are registered against. A computed relation:
	// a service needs no tuple storage, which is the point of registering against a
	// relation rather than against a table.
	Relation const * relation;
} fixture;

// number of services when starting test
static size32 initialNServices;

static IOSignature const exampleIOSignature = {.parameterIO = {
	PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT, PARAMETER_OUT}};


/**
 * Setup a relation (foo:INT bar:INT bar:INT baz:INT), but no relation table
 */
static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Atom formula = CStringToTerm("foo 0 bar 0 bar 0 baz 0");
	fixture.form = FormulaGetForm(formula);
	byte atomTypes[EXAMPLE_FORM_ARITY];
	SetMemory(atomTypes, EXAMPLE_FORM_ARITY, AT_INT);
	fixture.typeSignature = CreateTypeSignature(atomTypes, EXAMPLE_FORM_ARITY);
	IFactAcquire(fixture.form);
	ReleaseFormula(formula);

	// services are registered per relation, so we need one to test with
	fixture.relation = CreateRelation(fixture.form, EXAMPLE_FORM_ARITY, fixture.typeSignature);
}


// A machine operator has to be given a provider, though nothing here evaluates one
static MachineOperatorProvider dummyProvider = {
	.setupContext = 0,
	.call = 0,
	.finalizeContext = 0,
	.finalizeOperator = 0
};

/**
 * Create a dummy MACHINE operator of arity EXAMPLE_FORM_ARITY.
 * This operator cannot evaluate anything, only useful for testing the registry.
 */
static Operator * createDummyMachineOperator(void)
{
	return CreateMachineOperator(
		EXAMPLE_FORM_ARITY, (index8[]) {0, 1, 2, 3}, &dummyProvider, 0, 0);
}


/**
 * Register a service with a PERMUTE operators built on the given operator,
 * which must have arity = EXAMPLE_FORM_ARITY
 */
static Service createPermuteService(Relation const * relation, Operator * childOperator)
{
	Operator * op = CreatePermuteOperator(
		EXAMPLE_FORM_ARITY, 0, 0, 0, (index8[]) {0, 1, 2, 3}, childOperator);
	return CreateService(relation, exampleIOSignature, op, SERVICE_COMPILED);
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
	CreateService(fixture.relation, exampleIOSignature, op, SERVICE_PRIMITIVE);
	ASSERT_PTR_EQUAL(op->relation, fixture.relation)

	ASSERT_PTR_EQUAL(
		ServiceRegistryFind(fixture.relation, exampleIOSignature),
		op
	);

	// Removing the service removes the operator
	RemoveService(fixture.relation, op);

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
	CreateService(fixture.relation, exampleIOSignature, machineOperator, SERVICE_PRIMITIVE);

	// Hand-build a "compiled" service that depends on the machine service
	TypeSignature typeSignature1 = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_INT, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation const * relation1 = CreateRelation(fixture.form, EXAMPLE_FORM_ARITY, typeSignature1);
	Service service1 = createPermuteService(relation1, machineOperator);
	ReleaseRelation(relation1);
	ASSERT_FALSE(ServiceHasDependents(&service1))

	// A second "compiled" service that depends on the first one
	TypeSignature typeSignature2 = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_LETTER, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation const * relation2 = CreateRelation(fixture.form, EXAMPLE_FORM_ARITY, typeSignature2);
	Service service2 = createPermuteService(relation2, service1.op);
	ReleaseRelation(relation2);
	ASSERT_TRUE(ServiceHasDependents(&service1))
	// Nothing depends on the compiled service
	ASSERT_FALSE(ServiceHasDependents(&service2))

	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 2)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), initialNServices + 3)

	// Removing the machine service should remove both dependent services
	RemoveService(fixture.relation, machineOperator);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), initialNServices)
	// Both associated Relations should now be removed
	ASSERT_NULL(RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, typeSignature1))
	ASSERT_NULL(RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, typeSignature2))

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
	CreateService(fixture.relation, exampleIOSignature, machineOperator, SERVICE_PRIMITIVE);

	// Create a "compiled" relation depending on the machine service
	TypeSignature compiledTypes = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_INT, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation const * compiledRelation = CreateRelation(fixture.form, EXAMPLE_FORM_ARITY, compiledTypes);
	createPermuteService(compiledRelation, machineOperator);
	ReleaseRelation(compiledRelation);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)

	// Create a second relation of the fixture form, with distinct atom types,
	// and associated primitive services
	TypeSignature storedTypes = CreateTypeSignature(
		(byte[]) {AT_LETTER, AT_LETTER, AT_LETTER, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation const * storedRelation = CreateRelation(fixture.form, EXAMPLE_FORM_ARITY, storedTypes);
	RelationTable * storedTable = CreateRelationTable(
		storedRelation, &btreeStorageProvider, (index8[]) {0, 1, 2, 3});
	ReleaseRelation(storedRelation);
	// The above compiled service should now be invalidated
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_NULL(RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, compiledTypes))

	ReleaseRelationTable(storedTable);
	RemoveService(fixture.relation, machineOperator);
	teardownFixture();
}

/**
 * Register a compiled service (simulated) for the fixture relation, whose operator is shared
 * with a service of the given relation.
 */
static Service createServiceSharedOperator(Relation const * relation)
{
	// NOTE: the op here is an OPERATOR_MACHINE, which cannot be invalidated.
	Operator * op = ServiceRegistryFind(relation, exampleIOSignature);
	ASSERT_NOT_NULL(op)
	// This is COMPILED service, since it was not created by a storage provider.
	// NOTE: this will ASSERT in AttachOperator()
	return CreateService(fixture.relation, exampleIOSignature, op, SERVICE_COMPILED);
}


int main(void)
{
	KernelInitialize();
	ListSetup();
	StringSetup();
	initialNServices = ServiceRegistryCount();

	ExecuteTest(testAddRemoveService);
	ExecuteTest(testInvalidateDependentServices);
	ExecuteTest(testInvalidateOnPrimitiveService);

	StringShutdown();
	ListShutdown();
	KernelShutdown();

	TestSummary();
}
