
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/lookup.h"
#include "kernel/Relation.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/library.h"
#include "library/list.h"
#include "library/string.h"
#include "testing/testing.h"


void testLookup(void)
{
	Atom string = CreateStringFromCString("foo");
	Relation stringRelation = GetStringRelation();
	Atom termForm = stringRelation.form;
	Atom stringRole = GetStringRoleName();

	ASSERT_TRUE(LookupHasEntry(string, stringRelation, termForm, stringRole))

	// add 1 occurence of role
	LookupAddRole(string, stringRelation, termForm, stringRole);
	ASSERT_TRUE(LookupHasEntry(string, stringRelation, termForm, stringRole))
	
	LookupRemoveRole(string, stringRelation, termForm, stringRole);
	ASSERT_TRUE(LookupHasEntry(string, stringRelation, termForm, stringRole))

	// remove last occurence of role
	LookupRemoveRole(string, stringRelation, termForm, stringRole);
	ASSERT_FALSE(LookupHasEntry(string, stringRelation, termForm, stringRole))

	// restore role
	LookupAddRole(string, stringRelation, termForm, stringRole);
	ASSERT_TRUE(LookupHasEntry(string, stringRelation, termForm, stringRole))

	// add 1 occurence of role
	LookupAddRole(string, stringRelation, termForm, stringRole);
	ASSERT_TRUE(LookupHasEntry(string, stringRelation, termForm, stringRole))

	// remove both occurences
	LookupRemoveAllRoles(string);
	ASSERT_FALSE(LookupHasEntry(string, stringRelation, termForm, stringRole))

	IFactRelease(string);
}


/**
 * Test Adding and removing the roles of a predicate.
 */
void testLookupPredicateRoles(void)
{
	Atom roles[2] = {
		CreateNameFromCString("node"),
		CreateNameFromCString("weight")
	};
	Atom predicateForm = CreatePredicateForm(roles, 2);
	Atom termForm = CreateTermForm(predicateForm, true);
	index8 nodeIndex = PredicateRoleIndex(predicateForm, roles[0]);
	index8 weightIndex = PredicateRoleIndex(predicateForm, roles[1]);

	byte atomTypes[2];
	atomTypes[nodeIndex] = AT_ID;
	atomTypes[weightIndex] = AT_INT;
	TypeSignature typeSignature = CreateTypeSignature(atomTypes, 2);
	Relation relation = {.form = termForm, .typeSignature = typeSignature};
	AcquireRelation(relation);

	Atom node = CreateStringFromCString("foo");
	Atom actors[2];
	actors[nodeIndex] = node;
	actors[weightIndex] = (Atom) {._int = 42};

	// only the node column obtains a lookup record
	LookupAddFactRoles(relation, actors);
	ASSERT_TRUE(LookupHasEntry(node, relation, termForm, roles[0]))
	ASSERT_FALSE(LookupHasEntry(node, relation, termForm, roles[1]))

	LookupRemoveFactRoles(relation, actors);
	ASSERT_FALSE(LookupHasEntry(node, relation, termForm, roles[0]))

	ReleaseRelation(relation);
	IFactRelease(node);
	IFactRelease(termForm);
	IFactRelease(predicateForm);
	NameRelease(roles[0]);
	NameRelease(roles[1]);
}


void testLookupIterator(void)
{
	Atom string = CreateStringFromCString("foo");
	
	/**
	 * A string atom should have 3 lookup records:
	 * 
	 * (length list) list
	 * (position list element) list
	 * (string) string
	 */
	LookupIterator iterator;
	LookupIterate(string, &iterator);
	for(index32 i = 0; i < 3; i++) {
		ASSERT_TRUE(LookupIteratorNext(&iterator))
		Atom role = LookupIteratorGetRole(&iterator);
		// the role is either list or string
		ASSERT_TRUE(
			SameAtoms(role, GetListRoleName()) ||
			SameAtoms(role, GetStringRoleName())
		)
	}
	ASSERT_FALSE(LookupIteratorNext(&iterator))
	LookupIteratorEnd(&iterator);

	IFactRelease(string);
}

// NOTE: unclear if this functionis needed?

// void testRemoveAllPredicateRoles(void)
// {
// 	// create some AT_ID atoms
// 	Atom foo = CreateStringFromCString("foo");
// 	Atom bar = CreateStringFromCString("bar");
// 	Atom baz = CreateStringFromCString("baz");
	
// 	// create a new relation and assert some facts
// 	Atom foobar = CreateNameFromCString("foobar");
// 	Atom barf = CreateNameFromCString("barf");
// 	Atom form = CreatePredicateForm((Atom []) {foobar, barf}, 2);
// 	BTree * tree = CreateRelationBTree(2);
// 	RegistryAddBTreeService(form, tree);

// 	TypedTuple * actors1 = CreateTypedTupleFromArray(
// 		(TypedAtom[]) {CreateTypedAtom(AT_ID, foo), CreateTypedAtom(AT_ID, bar)},
// 		2
// 	);
// 	AssertFact(form, actors1);
// 	FreeTypedTuple(actors1);
// 	ASSERT_TRUE(AtomHasRole(foo, form, foobar))
// 	ASSERT_TRUE(AtomHasRole(bar, form, barf))

// 	TypedTuple * actors2 = CreateTypedTupleFromArray(
// 		(TypedAtom[]) {CreateTypedAtom(AT_ID, bar), CreateTypedAtom(AT_ID, baz)},
// 		2
// 	);
// 	AssertFact(form, actors2);
// 	FreeTypedTuple(actors2);
// 	ASSERT_TRUE(AtomHasRole(bar, form, foobar))
// 	ASSERT_TRUE(AtomHasRole(baz, form, barf))

// 	TypedTuple * actors3 = CreateTypedTupleFromArray(
// 		(TypedAtom[]) {CreateTypedAtom(AT_ID, foo), CreateTypedAtom(AT_ID, foo)},
// 		2
// 	);
// 	AssertFact(form, actors3);
// 	FreeTypedTuple(actors3);
// 	ASSERT_TRUE(AtomHasRole(foo, form, barf))

// 	LookupRemoveAllPredicateRoles(form);

// 	// all associations with the form should now be removed.
// 	ASSERT_FALSE(AtomHasRole(foo, form, (Atom) {0}))
// 	ASSERT_FALSE(AtomHasRole(bar, form, (Atom) {0}))
// 	ASSERT_FALSE(AtomHasRole(baz, form, (Atom) {0}))

// 	// remove corresponding relation table rows
// 	RelationBTreeRemoveTuples(tree, 0, REMOVE_NORMAL);

// 	// drop the relation table
// 	Service record = RegistryFindUntypedService(form);
// 	RegistryRemoveService(&record);
// 	FreeRelationBTree(tree);

// 	IFactRelease(form);
// 	IFactRelease(foo);
// 	IFactRelease(bar);
// 	IFactRelease(baz);
// 	NameRelease(foobar);
// 	NameRelease(barf);
// }


int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testLookup);
	ExecuteTest(testLookupPredicateRoles);
	ExecuteTest(testLookupIterator);
	// ExecuteTest(testRemoveAllPredicateRoles);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}

