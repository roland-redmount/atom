
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/RelationBTree.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "lang/ConjunctionForm.h"
#include "lang/Variable.h"
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


void testAddRelation(void)
{
	setupFixture();
	size32 nTablesInitial = RegistryNRelations();

	RelationTable const * createdTable = CreateRelationTable(
		&btreeTableProvider, fixture.form, EXAMPLE_FORM_ARITY,
		fixture.atomTypes, 0
	);
	ASSERT_UINT32_EQUAL(createdTable->nColumns, EXAMPLE_FORM_ARITY)

	RegistryAddRelationTable(createdTable);
	ASSERT_UINT32_EQUAL(RegistryNRelations(), nTablesInitial + 1)

	RelationTable const * foundTable = FindRelationTable(fixture.form, EXAMPLE_FORM_ARITY, fixture.atomTypes);
	ASSERT_PTR_EQUAL(foundTable, createdTable)

	RegistryRemoveRelationTable(createdTable);
	ASSERT_UINT32_EQUAL(RegistryNRelations(), nTablesInitial)
	
	FreeRelationTable(createdTable);
	teardownFixture();
}


void testAddRemoveService(void)
{
	// TODO
	ASSERT(false)
	// ServiceRecord record = RegistryFindUntypedService(fixture.form);
	// ASSERT(record.form.hash)
	// ASSERT(record.service->type == SERVICE_MACHINE)
}


// NOTE: move this to test_service ?
void testCallBTreeService(void)
{
	// Test calling 
	// (multiset @list-predicate-form element _ position _)
	Service * service = GetCoreService(SERVICE_MULTISET_NAME);
	ASSERT(service)
	ASSERT(service->type == SERVICE_MACHINE)

	Atom arguments[3];
	CoreFormSetTuple(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(Atom[]) {GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT), (Atom) {0}, (Atom) {0}},
		arguments
	);
	void * context = ServiceCreateContext(service, arguments);

	// this should yields 3 elements corresponding to the 3 roles of (list position element)
	size32 nElements = 0;
	while(ServiceCall(context))
		nElements++;
	ASSERT_INT32_EQUAL(nElements, 3);

	ServiceFreeContext(context);
}


int main(void)
{
	KernelInitialize();

	ExecuteTest(testAddRelation);
	ExecuteTest(testCallBTreeService);

	KernelShutdown();

	TestSummary();
}
