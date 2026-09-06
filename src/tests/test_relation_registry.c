
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/Relation.h"
#include "lang/formula.h"
#include "library/library.h"
#include "library/list.h"
#include "library/string.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


#define EXAMPLE_FORM_ARITY	4

struct {
	Atom form;		// a term form
	TypeSignature typeSignature;
} fixture;


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

	Relation relation = CreateRelation(fixture.form, fixture.typeSignature);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial + 1)
	ASSERT_TRUE(RelationExists(relation))

	// A second reference keeps the relation registered
	AcquireRelation(relation);
	ReleaseRelation(relation);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial + 1)

	// Releasing the creation reference removes it
	ReleaseRelation(relation);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial)
	ASSERT_FALSE(RelationExists(relation))

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
	Relation multisetName = GetCoreRelation(RELATION_MULTISET_NAME);
	Relation multisetId = GetCoreRelation(RELATION_MULTISET_ID);

	bool foundName = false;
	bool foundId = false;
	size32 nRelations = 0;

	RelationIterator iterator;
	RelationRegistryIterate(form, &iterator);
	while(RelationIteratorNext(&iterator)) {
		Relation relation = RelationIteratorGet(&iterator);
		// every relation yielded must belong to the form we asked for
		ASSERT_TRUE(SameAtoms(relation.termForm, form))
		if(SameRelations(relation, multisetName))
			foundName = true;
		if(SameRelations(relation, multisetId))
			foundId = true;
		nRelations++;
	}
	RelationIteratorEnd(&iterator);

	ASSERT_TRUE(foundName)
	ASSERT_TRUE(foundId)
	ASSERT_UINT32_EQUAL(nRelations, 2)

	// a form with a single relation
	form = GetListLengthTermForm();
	nRelations = 0;
	RelationRegistryIterate(form, &iterator);
	while(RelationIteratorNext(&iterator)) {
		ASSERT_TRUE(SameRelations(RelationIteratorGet(&iterator), GetListLengthRelation()))
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
	LoadLibraries();

	ExecuteTest(testAddRemoveRelation);
	ExecuteTest(testIterateRelations);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}
