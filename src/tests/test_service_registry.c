
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "lang/ConjunctionForm.h"
#include "lang/Variable.h"
#include "parser/PredicateBuilder.h"
#include "parser/ConjunctionBuilder.h"
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
	RegistryAddBTreeService(fixture.form, createdTable);
	ASSERT_UINT32_EQUAL(RegistryNServices(), nTablesInitial + 1)
	ASSERT_UINT32_EQUAL(RelationBTreeNColumns(createdTable), 4)

	ServiceRecord record = RegistryFindUntypedService(fixture.form);
	ASSERT(record.form)
	ASSERT(record.expression.type == EXPRESSION_MACHINE)
	BTree * foundTable = (BTree *) record.expression.value.machineService.providerData;
	ASSERT_PTR_NOT_EQUAL(foundTable, 0)
	ASSERT_PTR_EQUAL(foundTable, createdTable)

	RegistryRemoveService(&record);
	FreeRelationBTree(createdTable);
	ASSERT_UINT32_EQUAL(RegistryNServices(), nTablesInitial)
	
	teardownFixture();
}


void testCallBTreeService(void)
{
	// Test calling 
	// (multiset @list-predicate-form element _ position _)
	ServiceRecord const * record = RegistryGetCoreServiceRecord(FORM_MULTISET_ELEMENT_MULTIPLE);
	ASSERT(record)
	ASSERT(record->expression.type == EXPRESSION_MACHINE)
	Expression const * expression = &(record->expression);

	Tuple * arguments = CreateTuple(3);
	MultisetSetTuple(arguments,
		CreateTypedAtom(AT_ID, GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT)),
		anonymousVariable,
		anonymousVariable
	);
	void * context = ExpressionCreateContext(expression, arguments);

	// this should yields 3 elements corresponding to the 3 roles of (list position element)
	size32 nElements = 0;
	while(ExpressionCall(expression, context))
		nElements++;
	ASSERT_INT32_EQUAL(nElements, 3);

	ExpressionFreeContext(expression, context);
	FreeTuple(arguments);
}


int main(void)
{
	KernelInitialize();

	ExecuteTest(testAddDropTable);
	ExecuteTest(testCallBTreeService);

	KernelShutdown();

	TestSummary();
}
