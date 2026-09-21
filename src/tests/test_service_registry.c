
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
	Relation relation;
	TupleStore * store;
} fixture;

// number of services when starting test
static size32 initialNServices;

static IOSignature const exampleIOSignature = {.parameterIO = {
	PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT, PARAMETER_OUT}};


/**
 * Setup a relation (foo:INT bar:INT bar:INT baz:INT)
 * with the default storage provider and no services
 */
static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Atom formula = CStringToTerm("foo 0 bar 0 bar 0 baz 0");
	byte atomTypes[EXAMPLE_FORM_ARITY];
	SetMemory(atomTypes, EXAMPLE_FORM_ARITY, AT_INT);
	TypeSignature typeSignature = CreateTypeSignature(atomTypes, EXAMPLE_FORM_ARITY);
	fixture.relation = (Relation) {.termForm = FormulaGetForm(formula), .typeSignature = typeSignature};
	fixture.store = CreateTupleStore(fixture.relation, &defaultProvider, EXAMPLE_FORM_ARITY, 0);
	ReleaseFormula(formula);
}


/**
 * Add a dummy MACHINE operator to the given Relation.
 * This operator cannot evaluate anything, only useful for testing the registry.
 */
static Service addDummyMachineOperator(Relation relation)
{
	RelationReaderSpec dummyReaderspec = {.ioSignature = exampleIOSignature};
	Operator * op = CreateMachineOperator(EXAMPLE_FORM_ARITY, 0, &dummyReaderspec, 0);
	Service service = {.relation = relation, .ioSignature = exampleIOSignature};
	CreateService(service, op);
	return service;
}


/**
 * Register a service with a PERMUTE operators built on the given operator,
 * which must have arity = EXAMPLE_FORM_ARITY
 */
static Service createIdentityOpService(Relation relation, Operator * childOperator)
{
	Operator * op = CreateIdentityOperator(childOperator);
	Service service = {.relation = relation, .ioSignature = exampleIOSignature};
	CreateService(service, op);
	return service;
}


static void teardownFixture(void)
{
	DropRelation(fixture.relation);
}


void testAddRemoveService(void)
{
	setupFixture();

	// Add a dummy primitive service to the relation
	Service service = addDummyMachineOperator(fixture.store->relation);
	ASSERT_TRUE(SameRelations(service.relation, fixture.relation))
	ASSERT_NOT_NULL(FindServiceOperator(service));

	teardownFixture();
}


/**
 * Test that compiled services are removed when their dependencies are removed.
 */
void testInvalidateDependentServices(void)
{
	setupFixture();
	Service machineService = addDummyMachineOperator(fixture.store->relation);
	Operator * machineOp = FindServiceOperator(machineService);
	ASSERT(machineOp)

	// Hand-build a "compiled" service that depends on the machine service
	TypeSignature typeSignature1 = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_INT, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation relation1 = {.termForm = fixture.relation.termForm, .typeSignature = typeSignature1};
	// The Service will acquire the relation
	Service service1 = createIdentityOpService(relation1, machineOp);
	Operator * op1 = FindServiceOperator(service1);
	ASSERT_NOT_NULL(op1)
	ASSERT_INT32_EQUAL(op1->nParents, 0)

	// A second "compiled" service that depends on the first one
	TypeSignature typeSignature2 = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_LETTER, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation relation2 = {.termForm = fixture.relation.termForm, .typeSignature = typeSignature2};
	Service service2 = createIdentityOpService(relation2, op1);
	Operator * op2 = FindServiceOperator(service2);
	ASSERT_NOT_NULL(op2)
	// The first compiled operator now has parents
	ASSERT_FALSE(op1->nParents == 0)
	ASSERT_INT32_EQUAL(op2->nParents, 0)

	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 2)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 3)

	// Removing the machine service should remove both dependent services
	RemoveService(machineService);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices)
	// The associated Relations are removed as well
	ASSERT_FALSE(RelationExists(relation1))
	ASSERT_FALSE(RelationExists(relation2))

	teardownFixture();
}


/**
 * Registering a primitive service gives a query of its term form
 * one more relation to match, so compiled services depending on this form must be invalidated.
 */
void testInvalidateOnPrimitiveService(void)
{
	setupFixture();
	Service machineService = addDummyMachineOperator(fixture.store->relation);
	Operator * machineOp = FindServiceOperator(machineService);
	ASSERT(machineOp)

	// Create a "compiled" Service depending on the machine service
	TypeSignature compiledTypes = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_INT, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation compiledRelation = {.termForm = fixture.relation.termForm, .typeSignature = compiledTypes};
	createIdentityOpService(compiledRelation, machineOp);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 1)

	// Create a second relation of the fixture form, with distinct atom types,
	// and associated primitive services
	TypeSignature storedTypes = CreateTypeSignature(
		(byte[]) {AT_LETTER, AT_LETTER, AT_LETTER, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation storedRelation = {.termForm = fixture.relation.termForm, .typeSignature = storedTypes};
	CreateTupleStore(storedRelation, &btreeStorageProvider, EXAMPLE_FORM_ARITY, 0);

	// The compiled Service should now be invalidated (??)
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	ASSERT_FALSE(RelationExists(compiledRelation))
	ASSERT_TRUE(RelationExists(storedRelation))

	DropRelation(storedRelation);

	teardownFixture();
}


/**
 * Test dropping a relations with a compiled service
 */
void testDropCompiledService(void)
{
	// Setup fixture with a relation + tuple store with default provider
	setupFixture();
	size32 initialNRelations = RelationRegistryNRelations();

	// Add a primitive service to the fixture relation
	Service machineService = addDummyMachineOperator(fixture.store->relation);
	Operator * machineOp = FindServiceOperator(machineService);
	ASSERT(machineOp)
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 1)

	// Create "compiled" service for a separate relation, depending on the above service
	TypeSignature compiledTypes = CreateTypeSignature(
		(byte[]) {AT_INT, AT_INT, AT_INT, AT_LETTER}, EXAMPLE_FORM_ARITY);
	Relation compiledRelation = {.termForm = fixture.relation.termForm, .typeSignature = compiledTypes};
	// NOTE: this construction is incorrect, as compiledRelation has different
	// type signature than fixture.relation. Doesn't matter here though
	Service compiledService = createIdentityOpService(compiledRelation, machineOp);
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 2)
	ASSERT_TRUE(machineOp->nParents > 0)
	// Creating the service will register the relation
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations + 1)

	// Dropping the "compiled" relation also removes the compiled service
	DropRelation(compiledRelation);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations)
	ASSERT_FALSE(RelationExists(compiledRelation))
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 1)
	ASSERT_NULL(FindServiceOperator(compiledService))
	// The primitive service is still registered
	ASSERT_PTR_EQUAL(FindServiceOperator(machineService), machineOp)

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
	ExecuteTest(testDropCompiledService);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}
