
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "lang/formula.h"
#include "library/list.h"
#include "library/string.h"
#include "parser/TermBuilder.h"
#include "storage/RelationBTree.h"
#include "testing/testing.h"


#define EXAMPLE_FORM_ARITY	4

struct {
	Relation relation;
} fixture;

// number of services when starting test
static size32 initialNServices;

static IOSignature const exampleIOSignature = {.parameterIO = {
	PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT, PARAMETER_OUT}};


/**
 * Setup testRelation = (foo:INT bar:INT bar:INT baz:INT)
 */
static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Atom formula = CStringToTerm("foo 0 bar 0 bar 0 baz 0");
	byte atomTypes[EXAMPLE_FORM_ARITY];
	SetMemory(atomTypes, EXAMPLE_FORM_ARITY, AT_INT);
	TypeSignature typeSignature = CreateTypeSignature(atomTypes, EXAMPLE_FORM_ARITY);
	fixture.relation = CreateRelation(FormulaGetForm(formula),typeSignature);

	ReleaseFormula(formula);
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
static Service createPermuteService(Relation relation, Operator * childOperator)
{
	Operator * op = CreatePermuteOperator(
		EXAMPLE_FORM_ARITY, 0, 0, 0, (index8[]) {0, 1, 2, 3}, childOperator);
	return CreateService(relation, exampleIOSignature, op, SERVICE_COMPILED);
}


static void teardownFixture(void)
{
	ReleaseRelation(fixture.relation);
}


void testAddRemoveService(void)
{
	setupFixture();

	// Add a dummy service to the relation
	Operator * op = createDummyMachineOperator();
	CreateService(fixture.relation, exampleIOSignature, op, SERVICE_PRIMITIVE);
	ASSERT_TRUE(SameRelations(op->relation, fixture.relation))

	ASSERT_PTR_EQUAL(
		FindService(fixture.relation, exampleIOSignature),
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
	Relation relation1 = CreateRelation(fixture.relation.termForm, typeSignature1);
	Service service1 = createPermuteService(relation1, machineOperator);
	ReleaseRelation(relation1);
	ASSERT_FALSE(ServiceHasDependents(&service1))

	// A second "compiled" service that depends on the first one
	TypeSignature typeSignature2 = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_LETTER, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation relation2 = CreateRelation(fixture.relation.termForm, typeSignature2);
	Service service2 = createPermuteService(relation2, service1.op);
	ReleaseRelation(relation2);
	ASSERT_TRUE(ServiceHasDependents(&service1))
	// Nothing depends on the compiled service
	ASSERT_FALSE(ServiceHasDependents(&service2))

	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 2)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 3)

	// Removing the machine service should remove both dependent services
	RemoveService(fixture.relation, machineOperator);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices)
	// Both associated Relations should now be removed
	ASSERT_FALSE(RelationExists(relation1))
	ASSERT_FALSE(RelationExists(relation2))

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

	// Create a "compiled" Service depending on the machine service
	TypeSignature compiledTypes = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_INT, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation compiledRelation = CreateRelation(fixture.relation.termForm, compiledTypes);
	createPermuteService(compiledRelation, machineOperator);
	ReleaseRelation(compiledRelation);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 1)

	// Create a second relation of the fixture form, with distinct atom types,
	// and associated primitive services
	TypeSignature storedTypes = CreateTypeSignature(
		(byte[]) {AT_LETTER, AT_LETTER, AT_LETTER, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation storedRelation = CreateRelation(fixture.relation.termForm, storedTypes);
	RelationTable * storedTable = CreateRelationTable(
		storedRelation, &btreeStorageProvider, (index8[]) {0, 1, 2, 3});
	ReleaseRelation(storedRelation);
	// The compiled Service should now be invalidated, and the Relation dropped
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	ASSERT_FALSE(RelationExists(compiledRelation))

	ReleaseRelationTable(storedTable);
	RemoveService(fixture.relation, machineOperator);
	teardownFixture();
}


int main(void)
{
	KernelInitialize();
	ListSetup();
	StringSetup();
	initialNServices = NumberOfServices();

	ExecuteTest(testAddRemoveService);
	ExecuteTest(testInvalidateDependentServices);
	ExecuteTest(testInvalidateOnPrimitiveService);

	StringShutdown();
	ListShutdown();
	KernelShutdown();

	TestSummary();
}
