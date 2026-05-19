
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "lang/ConjunctionForm.h"
#include "lang/Variable.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"


struct {
	Atom form;		// a form
} fixture;

static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Atom formula = CStringToPredicate("foo 0 bar 0 bar 0 baz 0");
	fixture.form = FormulaGetForm(formula);
	IFactAcquire(fixture.form);
	IFactRelease(formula);
}

static void teardownFixture(void)
{
	IFactRelease(fixture.form);
}


void testAddDropTable(void)
{
	setupFixture();
	size32 nTablesInitial = RegistryNServices();

	BTree * createdTable = CreateRelationBTree(4);
	Atom service = RegistryAddBTreeService(fixture.form, createdTable);
	ASSERT_UINT32_EQUAL(RegistryNServices(), nTablesInitial + 1)
	ASSERT_UINT32_EQUAL(RelationBTreeNColumns(createdTable), 4)

	ServiceRecord record = RegistryFindUntypedService(fixture.form);
	ASSERT(record.type == SERVICE_MACHINE)
	BTree * foundTable = (BTree *) record.provider.machineService.serviceParameter;
	ASSERT_PTR_NOT_EQUAL(foundTable, 0)
	ASSERT_PTR_EQUAL(foundTable, createdTable)

	RegistryRemoveService(service);
	ASSERT_UINT32_EQUAL(RegistryNServices(), nTablesInitial)
	
	teardownFixture();
}


void testCallBTreeService(void)
{
	// Test calling 
	// (multiset @list-predicate-form element _ position _)
	ServiceRecord record = RegistryGetCoreServiceRecord(FORM_MULTISET_ELEMENT_MULTIPLE);
	ASSERT(record.type == SERVICE_MACHINE)

	// Create execution context
	MachineService * machineService = &(record.provider.machineService);

	Tuple * arguments = CreateTuple(3);
	MultisetSetTuple(arguments,
		CreateTypedAtom(AT_ID, GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT)),
		anonymousVariable,
		anonymousVariable
	);
	
	void * context = machineService->setupContext(machineService, arguments);

	// this should yields 3 elements corresponding to the 3 roles of (list position element)
	size32 nElements = 0;
	while(machineService->call(context, arguments))
		nElements++;
	ASSERT_INT32_EQUAL(nElements, 3);

	machineService->finalizeContext(context);
	FreeTuple(arguments);
}


void testJoinService(void)
{
	/**
	 * As an example, consider the query
	 * 
	 *  (predicate-form p) & (multiset p element e multiple m)
	 * 
	 * This has actors (p, p, e, m) which must be mapped to
	 * the arguments of (predicate-form p) and (multiset p element e multiple m).
	 */

	// Conjunction form 
	Atom leftForm = GetCorePredicateForm(FORM_PREDICATE_FORM);
	PrintPredicateForm(leftForm);
	PrintChar('\n');
	Atom rightForm = GetCorePredicateForm(FORM_MULTISET_ELEMENT_MULTIPLE);
	PrintPredicateForm(rightForm);
	PrintChar('\n');

	// TODO: Parse this into a conjunction form
	// char const * formSyntax = "(predicate-form _p) & (multiset _p element _e multiple _m)";


	// Create the join service
	ServiceRecord left = RegistryGetCoreServiceRecord(FORM_PREDICATE_FORM);
	ServiceRecord right = RegistryGetCoreServiceRecord(FORM_MULTISET_ELEMENT_MULTIPLE);

}


int main(void)
{
	KernelInitialize();

	ExecuteTest(testAddDropTable);
	ExecuteTest(testCallBTreeService);
	ExecuteTest(testJoinService);

	KernelShutdown();

	TestSummary();
}
