
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "lang/formula.h"
#include "library/library.h"
#include "library/MachineService.h"
#include "library/string.h"
#include "parser/TermBuilder.h"
#include "storage/RelationBTree.h"
#include "testing/testing.h"


#define EXAMPLE_FORM_ARITY	4

struct {
	RelationSignature relation;
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
	fixture.relation = CreateRelation(FormulaGetForm(formula), typeSignature, 0, 0);

	ReleaseFormula(formula);
}


/**
 * Create a dummy MACHINE operator of arity EXAMPLE_FORM_ARITY.
 * This operator cannot evaluate anything, only useful for testing the registry.
 */
static Operator * createDummyMachineOperator(void)
{
	return CreateMachineOperator(
		EXAMPLE_FORM_ARITY, (index8[]) {0, 1, 2, 3}, 0
	);
}


/**
 * Register a service with a PERMUTE operators built on the given operator,
 * which must have arity = EXAMPLE_FORM_ARITY
 */
static Service createPermuteService(RelationSignature relation, Operator * childOperator)
{
	Operator * op = CreatePermuteOperator(
		EXAMPLE_FORM_ARITY, 0, 0, 0, (index8[]) {0, 1, 2, 3}, childOperator);
	return CreateService(relation, exampleIOSignature, op);
}


static void teardownFixture(void)
{
	DropRelation(fixture.relation);
}


void testAddRemoveService(void)
{
	setupFixture();

	// Add a dummy service to the relation
	Operator * op = createDummyMachineOperator();
	CreateService(fixture.relation, exampleIOSignature, op);
	ASSERT_TRUE(SameRelations(op->relation, fixture.relation))

	ASSERT_PTR_EQUAL(
		FindServiceOperator(fixture.relation, exampleIOSignature),
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
	CreateService(fixture.relation, exampleIOSignature, machineOperator);

	// Hand-build a "compiled" service that depends on the machine service
	TypeSignature typeSignature1 = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_INT, AT_LETTER}, EXAMPLE_FORM_ARITY);
	RelationSignature relation1 = CreateRelation(fixture.relation.termForm, typeSignature1, 0, 0);
	Service service1 = createPermuteService(relation1, machineOperator);
	ASSERT_INT32_EQUAL(service1.op->nParents, 0)

	// A second "compiled" service that depends on the first one
	TypeSignature typeSignature2 = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_LETTER, AT_LETTER}, EXAMPLE_FORM_ARITY);
	RelationSignature relation2 = CreateRelation(fixture.relation.termForm, typeSignature2, 0, 0);
	Service service2 = createPermuteService(relation2, service1.op);
	ASSERT_FALSE(service1.op->nParents == 0)
	// Nothing depends on the compiled service
	ASSERT_INT32_EQUAL(service2.op->nParents, 0)

	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 2)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 3)

	// Removing the machine service should remove both dependent services
	RemoveService(fixture.relation, machineOperator);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices)
	// Both associated Relations should now be removed
	ASSERT_FALSE(RelationExists(relation1))
	ASSERT_FALSE(RelationExists(relation2))

	DropRelation(relation2);
	DropRelation(relation1);
	
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
	CreateService(fixture.relation, exampleIOSignature, machineOperator);

	// Create a "compiled" Service depending on the machine service
	TypeSignature compiledTypes = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_INT, AT_LETTER}, EXAMPLE_FORM_ARITY);
	RelationSignature compiledRelation = CreateRelation(fixture.relation.termForm, compiledTypes, 0, 0);
	createPermuteService(compiledRelation, machineOperator);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 1)

	// Create a second relation of the fixture form, with distinct atom types,
	// and associated primitive services
	TypeSignature storedTypes = CreateTypeSignature(
		(byte[]) {AT_LETTER, AT_LETTER, AT_LETTER, AT_LETTER}, EXAMPLE_FORM_ARITY);
	RelationSignature storedRelation = CreateRelation(
		fixture.relation.termForm, storedTypes, &btreeStorageProvider, (index8[]) {0, 1, 2, 3});

	// The compiled Service should now be invalidated
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	// The relations still exit
	ASSERT_TRUE(RelationExists(compiledRelation))
	ASSERT_TRUE(RelationExists(storedRelation))

	DropRelation(compiledRelation);
	DropRelation(storedRelation);
	RemoveService(fixture.relation, machineOperator);
	teardownFixture();
}


/*
 * CLAUDE: A signature no B-tree storage service provides, so that a computed service
 * can share the fixture relation with a RelationWriter. A B-tree registers one service
 * per prefix key, which for arity 4 gives IIII, IIIO, IIOO, IOOO and OOOO.
 * The fixture form repeats the role "bar", and this signature is unchanged by the
 * permutation swapping the two occurrences; see the note on CreateService().
 */
static IOSignature const computedIOSignature = {.parameterIO = {
	PARAMETER_OUT, PARAMETER_IN, PARAMETER_IN, PARAMETER_IN}};


/**
 * Test that computed services attached to a relation survive removing
 * the relationstorage.
 */
void testRemoveRelationStorage(void)
{
	setupFixture();
	size32 initialNRelations = RelationRegistryNRelations();

	RelationSignature relation = CreateRelation(
		fixture.relation.termForm, fixture.relation.typeSignature,
		&btreeStorageProvider, (index8[]) {0, 1, 2, 3});
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations + 1)

	// Add a computed service for the relation 
	Operator * computedOperator = createDummyMachineOperator();
	CreateService(fixture.relation, computedIOSignature, computedOperator);

	// TODO: how to remove the relation table storage

	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations)

	// Only the storage services went with the table
	ASSERT_PTR_EQUAL(FindServiceOperator(fixture.relation, computedIOSignature), computedOperator)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 1)

	RemoveService(fixture.relation, computedOperator);
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices)
	teardownFixture();
}


/**
 * A service depending on a computed service does not read from storage,
 * and so does not keep the RelationWriter of the associated relation alive.
 */
void testDropCompiledService(void)
{
	setupFixture();
	size32 initialNRelations = RelationRegistryNRelations();
	// A B-tree backed relation for the fixture relation
	RelationSignature btreeRelation = CreateRelation(
		fixture.relation.termForm, fixture.relation.typeSignature,
		&btreeStorageProvider, (index8[]) {0, 1, 2, 3});
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations + 1)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 5)

	// A computed service of the same relation
	Operator * computedOperator = createDummyMachineOperator();
	CreateService(fixture.relation, computedIOSignature, computedOperator);
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 6)

	// A "compiled" service for a separate relation, depending on the computed service
	TypeSignature compiledTypes = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_INT, AT_LETTER}, EXAMPLE_FORM_ARITY);
	RelationSignature compiledRelation = CreateRelation(
		fixture.relation.termForm, compiledTypes, 0, 0);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations + 2)
	Service compiled = createPermuteService(compiledRelation, computedOperator);
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 7)
	ASSERT_TRUE(computedOperator->nParents > 0)

	// Dropping the "compiled" relationervice does not affect the computed relation
	DropRelation(compiledRelation);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations + 1)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 6)

	// Both the computed service and its dependent are still registered
	ASSERT_PTR_EQUAL(FindServiceOperator(fixture.relation, computedIOSignature), computedOperator)
	ASSERT_PTR_EQUAL(FindServiceOperator(compiledRelation, exampleIOSignature), compiled.op)

	// Removing the computed service does not affect the B-tree services
	RemoveService(fixture.relation, computedOperator);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations + 1)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 5)
	ASSERT_FALSE(RelationExists(compiledRelation))

	DropRelation(btreeRelation);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices)

	teardownFixture();
}


int main(void)
{
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();
	initialNServices = NumberOfServices();

	ExecuteTest(testAddRemoveService);
	ExecuteTest(testInvalidateDependentServices);
	ExecuteTest(testInvalidateOnPrimitiveService);
	// ExecuteTest(testComputedServiceOutlivesTable);
	ExecuteTest(testDropCompiledService);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}
