
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/lookup.h"
#include "kernel/RelationTable.h"
#include "kernel/string.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "testing/testing.h"


void testLookup(void)
{
	Atom string = CreateStringFromCString("foo");
	RelationTable const * stringRelation = GetCoreRelationTable(RELATION_STRING);
	Atom stringRole = GetCoreRoleName(ROLE_STRING);

	ASSERT_TRUE(AtomHasRole(string, stringRelation, stringRole))
	ASSERT_TRUE(AtomHasRole(string, stringRelation, (Atom) {0}))
	ASSERT_TRUE(AtomHasRole(string, 0, (Atom) {0}))

	// add 1 occurence of role
	AtomAddRole(string, stringRelation, stringRole);
	ASSERT_TRUE(AtomHasRole(string, stringRelation, stringRole))
	
	AtomRemoveRole(string, stringRelation, stringRole);
	ASSERT_TRUE(AtomHasRole(string, stringRelation, stringRole))

	// remove last occurence of role
	AtomRemoveRole(string, stringRelation, stringRole);
	ASSERT_FALSE(AtomHasRole(string, stringRelation, stringRole))

	// restore role
	AtomAddRole(string, stringRelation, stringRole);
	ASSERT_TRUE(AtomHasRole(string, stringRelation, stringRole))

	// add 1 occurence of role
	AtomAddRole(string, stringRelation, stringRole);
	ASSERT_TRUE(AtomHasRole(string, stringRelation, stringRole))

	// remove both occurences
	LookupRemoveAllRoles(string);
	ASSERT_FALSE(AtomHasRole(string, stringRelation, stringRole))

	IFactRelease(string);
}


/**
 * Adding and removing the roles of a predicate must visit the same columns.
 * Only AT_ID columns obtain a lookup record, so a relation with a column of
 * another type tells the two apart.
 */
void testLookupPredicateRoles(void)
{
	Atom roles[2] = {
		CreateNameFromCString("node"),
		CreateNameFromCString("weight")
	};
	Atom form = CreatePredicateForm(roles, 2);
	index8 nodeIndex = PredicateRoleIndex(form, roles[0]);
	index8 weightIndex = PredicateRoleIndex(form, roles[1]);

	byte atomTypes[2];
	atomTypes[nodeIndex] = AT_ID;
	atomTypes[weightIndex] = AT_UINT;
	// a computed relation, as we only need it to describe the columns
	RelationTable const * relation = CreateRelationTable(0, form, 2, atomTypes, 0);

	Atom node = CreateStringFromCString("foo");
	Atom actors[2];
	actors[nodeIndex] = node;
	actors[weightIndex] = (Atom) {._uint = 42};

	// only the node column obtains a lookup record
	LookupAddPredicateRoles(relation, actors);
	ASSERT_TRUE(AtomHasRole(node, relation, roles[0]))
	ASSERT_FALSE(AtomHasRole(node, relation, roles[1]))

	LookupRemovePredicateRoles(relation, actors);
	ASSERT_FALSE(AtomHasRole(node, relation, roles[0]))

	IFactRelease(node);
	FreeRelationTable(relation);
	IFactRelease(form);
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
			(role.hash == GetCoreRoleName(ROLE_LIST).hash) ||
			(role.hash == GetCoreRoleName(ROLE_STRING).hash)
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
// 	ServiceRecord record = RegistryFindUntypedService(form);
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
	KernelInitialize();

	ExecuteTest(testLookup);
	ExecuteTest(testLookupPredicateRoles);
	ExecuteTest(testLookupIterator);
	// ExecuteTest(testRemoveAllPredicateRoles);

	KernelShutdown();

	TestSummary();
}

