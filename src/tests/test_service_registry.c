
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


struct {
	Relation relation;
	TupleStore * store;
} fixture;

// number of services when starting test
static size32 initialNServices;

/**
 * Setup a relation (foo:INT bar:INT)
 * with the default storage provider and no services
 */
static void setupFixture(void)
{
	Atom formula = CStringToTerm("foo 0 bar 0");
	fixture.relation = RelationFromFact(FormulaGetView(formula));
	fixture.store = CreateTupleStore(fixture.relation, &defaultProvider, 2, 0);
	ReleaseFormula(formula);
}


static IOSignature const firstInputIOSignature = {
	.parameterIO = {PARAMETER_IN, PARAMETER_OUT}
};


/**
 * Add a dummy MACHINE operator to the given Relation.
 * This operator cannot evaluate anything, only useful for testing the registry.
 */
static Service addDummyMachineOperator(Relation relation)
{
	RelationReaderSpec dummyReaderspec = {.ioSignature = firstInputIOSignature};
	Operator * op = CreateMachineOperator(2, 0, &dummyReaderspec, 0);
	Service service = {.relation = relation, .ioSignature = dummyReaderspec.ioSignature};
	CreateService(service, op);
	return service;
}


/**
 * Register a service with a PERMUTE operators built on the given operator,
 * which must have arity = EXAMPLE_FORM_ARITY
 */
static Service createIdentityOpService(
	Relation relation, IOSignature ioSignature, Operator * childOperator)
{
	Operator * op = CreateIdentityOperator(childOperator);
	Service service = {.relation = relation, .ioSignature = ioSignature};		// exampleIOSignature 
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
	ASSERT_NOT_NULL(ServiceGetOperator(service));

	teardownFixture();
}


/**
 * Test that compiled services are removed when their dependencies are removed.
 */
void testInvalidateDependentServices(void)
{
	setupFixture();
	Service machineService = addDummyMachineOperator(fixture.store->relation);
	Operator * machineOp = ServiceGetOperator(machineService);
	ASSERT(machineOp)

	// Hand-build a "compiled" service that depends on the machine service
	TypeSignature typeSignature1 = CreateTypeSignature((byte[]) {AT_INT, AT_LETTER}, 2);
	Relation relation1 = {.form = fixture.relation.form, .typeSignature = typeSignature1};
	// The Service will acquire the relation
	Service service1 = createIdentityOpService(relation1, firstInputIOSignature, machineOp);
	Operator * op1 = ServiceGetOperator(service1);
	ASSERT_NOT_NULL(op1)
	ASSERT_INT32_EQUAL(op1->nParents, 0)

	// A second "compiled" service that depends on the first one
	TypeSignature typeSignature2 = CreateTypeSignature((byte[]) {AT_LETTER, AT_LETTER}, 2);
	Relation relation2 = {.form = fixture.relation.form, .typeSignature = typeSignature2};
	Service service2 = createIdentityOpService(relation2, firstInputIOSignature, op1);
	Operator * op2 = ServiceGetOperator(service2);
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
 * Registering a primitive service of the same signature as an existing compiled service
 * should remove the compiled service and mark the primitive service stale.
 */
void testInvalidateOnPrimitiveService(void)
{
	setupFixture();
	Service machineService = addDummyMachineOperator(fixture.store->relation);
	Operator * machineOp = ServiceGetOperator(machineService);
	ASSERT(machineOp)

	// Create a "compiled" Service depending on the machine service
	TypeSignature compiledTypes = CreateTypeSignature((byte[]) {AT_INT, AT_LETTER}, 2);
	Relation relation = {.form = fixture.relation.form, .typeSignature = compiledTypes};
	Service service = createIdentityOpService(relation, firstInputIOSignature, machineOp);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 1)
	Operator * compiledOperator = ServiceGetOperator(service);
	ASSERT_NOT_NULL(compiledOperator)

	// Create a TupleStore to the relation, adding a new service whose signature
	// is the same as compiledService
	CreateTupleStore(relation, &btreeStorageProvider, 2, 0);

	// The compiled operator should now be detached, and the service should
	// point to the machine operator from the tuples store, marked stale
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	ASSERT_TRUE(RelationExists(relation))
	ServiceRecord const * record = ServiceGetRecord(service);
	ASSERT_NOT_NULL(record)
	ASSERT_INT32_EQUAL(record->op->type, OPERATOR_MACHINE)
	ASSERT_TRUE(SameRelations(record->op->relation, relation))
	ASSERT_TRUE(ServiceIsStale(service))

	DropRelation(relation);

	teardownFixture();
}


/**
 * Test dropping a relation with a compiled service
 */
void testDropRelatonWithCompiledService(void)
{
	// Setup fixture with a relation + tuple store with default provider
	setupFixture();
	size32 initialNRelations = RelationRegistryNRelations();

	// Add a primitive service to the fixture relation
	Service machineService = addDummyMachineOperator(fixture.store->relation);
	Operator * machineOp = ServiceGetOperator(machineService);
	ASSERT(machineOp)
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations)
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 1)

	// Create "compiled" service for a separate relation, depending on the above service
	TypeSignature compiledTypes = CreateTypeSignature((byte[]) {AT_INT, AT_LETTER}, 2);
	Relation compiledRelation = {.form = fixture.relation.form, .typeSignature = compiledTypes};
	// NOTE: this construction is incorrect, as compiledRelation has different
	// type signature than fixture.relation. Doesn't matter here though
	Service compiledService = createIdentityOpService(compiledRelation, firstInputIOSignature, machineOp);
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 2)
	ASSERT_TRUE(machineOp->nParents > 0)
	// Creating the service will register the relation
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations + 1)

	// Dropping the "compiled" relation also removes the compiled service
	DropRelation(compiledRelation);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), initialNRelations)
	ASSERT_FALSE(RelationExists(compiledRelation))
	ASSERT_UINT32_EQUAL(NumberOfServices(), initialNServices + 1)
	ASSERT_NULL(ServiceGetOperator(compiledService))
	// The primitive service is still registered
	ASSERT_PTR_EQUAL(ServiceGetOperator(machineService), machineOp)

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
	ExecuteTest(testDropRelatonWithCompiledService);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}
