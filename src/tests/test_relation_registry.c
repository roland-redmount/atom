
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "lang/Formula.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


#define EXAMPLE_FORM_ARITY	4

struct {
	Atom form;		// a term form
	byte atomTypes[EXAMPLE_FORM_ARITY];
} fixture;


static void setupFixture(void)
{
	// TODO: we should have a way to parse a form from a C string.
	Formula * formula = CStringToTerm("foo 0 bar 0 bar 0 baz 0");
	fixture.form = formula->form;
	SetMemory(fixture.atomTypes, EXAMPLE_FORM_ARITY, AT_INT);
	IFactAcquire(fixture.form);
	FreeFormula(formula);
}


static void teardownFixture(void)
{
	IFactRelease(fixture.form);
}


/**
 * A relation registers itself when created and removes itself when its last reference
 * goes; see Relation.h
 */
void testAddRemoveRelation(void)
{
	setupFixture();
	size32 nRelationsInitial = RelationRegistryNRelations();

	Relation const * relation = CreateRelation(
		fixture.form, EXAMPLE_FORM_ARITY, fixture.atomTypes);
	ASSERT_UINT32_EQUAL(relation->nColumns, EXAMPLE_FORM_ARITY)
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial + 1)

	ASSERT_PTR_EQUAL(
		RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, fixture.atomTypes),
		relation
	)

	// A second reference keeps the relation registered
	AcquireRelation(relation);
	ReleaseRelation(relation);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial + 1)

	// Releasing the creation reference removes it
	ReleaseRelation(relation);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial)

	ASSERT_NULL(RelationRegistryFind(fixture.form, EXAMPLE_FORM_ARITY, fixture.atomTypes));

	teardownFixture();
}


/**
 * The core term form (multiset element multiple) has two relations,
 * one for NAME elements and one for ID elements, so it is a good case
 * for iterating over the relations of a single form.
 */
void testIterateRelations(void)
{
	Atom form = GetCoreTermForm(FORM_MULTISET_ELEMENT_MULTIPLE);
	Relation const * multisetName = GetCoreRelation(RELATION_MULTISET_NAME);
	Relation const * multisetId = GetCoreRelation(RELATION_MULTISET_ID);

	bool foundName = false;
	bool foundId = false;
	size32 nRelations = 0;

	RelationIterator iterator;
	RelationRegistryIterate(form, &iterator);
	while(RelationIteratorNext(&iterator)) {
		Relation const * relation = RelationIteratorGet(&iterator);
		// every relation yielded must belong to the form we asked for
		ASSERT_DATA64_EQUAL(relation->termForm.hash, form.hash)
		if(relation == multisetName)
			foundName = true;
		if(relation == multisetId)
			foundId = true;
		nRelations++;
	}
	RelationIteratorEnd(&iterator);

	ASSERT_TRUE(foundName)
	ASSERT_TRUE(foundId)
	ASSERT_UINT32_EQUAL(nRelations, 2)

	// a form with a single relation
	form = GetCoreTermForm(FORM_LIST_LENGTH);
	nRelations = 0;
	RelationRegistryIterate(form, &iterator);
	while(RelationIteratorNext(&iterator)) {
		ASSERT_PTR_EQUAL(RelationIteratorGet(&iterator), GetCoreRelation(RELATION_LIST_LENGTH))
		nRelations++;
	}
	RelationIteratorEnd(&iterator);
	ASSERT_UINT32_EQUAL(nRelations, 1)

	// an unregistered form yields nothing
	setupFixture();
	RelationRegistryIterate(fixture.form, &iterator);
	ASSERT_FALSE(RelationIteratorNext(&iterator))
	RelationIteratorEnd(&iterator);
	teardownFixture();
}


int main(void)
{
	KernelInitialize();

	ExecuteTest(testAddRemoveRelation);
	ExecuteTest(testIterateRelations);

	KernelShutdown();

	TestSummary();
}
