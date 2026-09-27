
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


static struct {
	Atom roles[2];
	index8 nodeIndex;
	index8 weightIndex;
	Atom termForm;
	Relation relation;
} relationFixture;


static void createRelationFixture(void)
{
	relationFixture.roles[0] = CreateNameFromCString("node");
	relationFixture.roles[1] = CreateNameFromCString("weight");

	Atom predicateForm = CreatePredicateForm(relationFixture.roles, 2);
	relationFixture.termForm = CreateTermForm(predicateForm, true);
	relationFixture.nodeIndex = PredicateRoleIndex(predicateForm, relationFixture.roles[0]);
	relationFixture.weightIndex = PredicateRoleIndex(predicateForm, relationFixture.roles[1]);

	byte atomTypes[2];
	atomTypes[relationFixture.nodeIndex] = AT_ID;
	atomTypes[relationFixture.weightIndex] = AT_INT;
	TypeSignature typeSignature = CreateTypeSignature(atomTypes, 2);
	relationFixture.relation = (Relation) {
		.form = relationFixture.termForm, .typeSignature = typeSignature};
	AcquireRelation(relationFixture.relation);

	IFactRelease(predicateForm);
}


static void teardownRelationFixture(void)
{
	ReleaseRelation(relationFixture.relation);
	IFactRelease(relationFixture.termForm);
	NameRelease(relationFixture.roles[0]);
	NameRelease(relationFixture.roles[1]);
}


void testLookup(void)
{
	createRelationFixture();
	Atom node = CreateStringFromCString("foo");

	// add 1 occurence of role
	LookupAddRole(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[0]);
	ASSERT_TRUE(LookupHasEntry(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[0]))
	ASSERT_FALSE(LookupHasEntry(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[1]))

	// add a second occurence
	LookupAddRole(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[0]);
	ASSERT_TRUE(LookupHasEntry(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[0]))

	// remove one occurence
	LookupRemoveRole(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[0]);
	ASSERT_TRUE(LookupHasEntry(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[0]))

	// remove last occurence of role
	LookupRemoveRole(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[0]);
	ASSERT_FALSE(LookupHasEntry(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[0]))

	IFactRelease(node);
	teardownRelationFixture();
}


/**
 * Test Adding and removing the roles of a specific fact.
 */
void testLookupFactRoles(void)
{
	createRelationFixture();

	Atom node = CreateStringFromCString("foo");
	Atom actors[2];
	actors[relationFixture.nodeIndex] = node;
	actors[relationFixture.weightIndex] = (Atom) {._int = 42};

	// only the node column obtains a lookup record
	LookupAddFactRoles(relationFixture.relation, actors);
	ASSERT_TRUE(LookupHasEntry(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[0]))
	ASSERT_FALSE(LookupHasEntry(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[1]))

	LookupRemoveFactRoles(relationFixture.relation, actors);
	ASSERT_FALSE(LookupHasEntry(node, relationFixture.relation, relationFixture.termForm, relationFixture.roles[0]))

	IFactRelease(node);
	teardownRelationFixture();
}


void testLookupIterator(void)
{
	createRelationFixture();

	Atom node = CreateStringFromCString("foo");
	Atom actors[2];
	actors[relationFixture.nodeIndex] = node;
	actors[relationFixture.weightIndex] = (Atom) {._int = 7};
	LookupAddFactRoles(relationFixture.relation, actors);
	
	LookupIterator iterator;
	LookupIterate(node, &iterator);

	ASSERT_TRUE(LookupIteratorNext(&iterator))
	Atom role = LookupIteratorGetRole(&iterator);
	ASSERT_TRUE(SameAtoms(role, relationFixture.roles[0]))
	
	ASSERT_FALSE(LookupIteratorNext(&iterator))
	LookupIteratorEnd(&iterator);

	LookupRemoveFactRoles(relationFixture.relation, actors);
	IFactRelease(node);
	teardownRelationFixture();
}


int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testLookup);
	ExecuteTest(testLookupFactRoles);
	ExecuteTest(testLookupIterator);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}

