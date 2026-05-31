
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
	Atom service = RegistryAddBTreeService(fixture.form, createdTable);
	ASSERT_UINT32_EQUAL(RegistryNServices(), nTablesInitial + 1)
	ASSERT_UINT32_EQUAL(RelationBTreeNColumns(createdTable), 4)

	ServiceRecord record = RegistryFindUntypedService(fixture.form);
	ASSERT(record.service)
	ASSERT(record.expression.type == EXPRESSION_MACHINE)
	BTree * foundTable = (BTree *) record.expression.value.machineService.providerData;
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
	ASSERT(record.service)
	ASSERT(record.expression.type == EXPRESSION_MACHINE)
	Expression const * expression = &(record.expression);

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


void testJoinService(void)
{
	/**
	 * As an example, consider the rule
	 * 
	 * predicate-form p role r multiple m <-
	 *   predicate-form p & multiset p element r multiple m
	 * 
	 * The LHS will compile to a SERVICE_JOIN over the two machine services
	 * for (predicate-form) and (multiset element multiple). Essentially,
	 * (predicate-form p role r multiple m) becomes a synonym for the query
	 * (predicate-form p & multiset p element r multiple m). This suggests we
	 * should store the SERVICE_JOIN service once and make an n:1 mapping
	 * between forms and services.
	 * 
	 * As another example, consider
	 *
	 * predicate-form p role r <-
	 *   predicate-form p & multiset p element r multiple _
	 *  
	 * Here we are dropping the "multiset" role from the (multiset element multiple)
	 * relation; this is another service, say SERVICE_PROJECT, that applies the
	 * projection operator from relational algebra (remove columns and take the
	 * resulting unique tuples).
	 * 
	 * This has actors (p, p, e, m) which must be mapped to
	 * the arguments of (predicate-form p) and (multiset p element e multiple m).
	 */

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
