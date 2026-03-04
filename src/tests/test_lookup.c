
#include "kernel/kernel.h"
#include "kernel/lookup.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "testing/testing.h"


void testLookup(void)
{
	Atom string = CreateStringFromCString("foo");
	Atom stringForm = GetCorePredicateForm(FORM_STRING);
	Atom stringRole = GetCoreRoleName(ROLE_STRING);

	ASSERT_TRUE(AtomHasRole(string, stringForm, stringRole))

	// add 1 occurence of role
	AtomAddRole(string, stringForm, stringRole);
	ASSERT_TRUE(AtomHasRole(string, stringForm, stringRole))
	ASSERT_TRUE(AtomHasRole(string, stringForm, invalidAtom))
	ASSERT_TRUE(AtomHasRole(string, invalidAtom, invalidAtom))
	
	AtomRemoveRole(string, stringForm, stringRole);
	ASSERT_TRUE(AtomHasRole(string, stringForm, stringRole))

	// remove last occurence of role
	AtomRemoveRole(string, stringForm, stringRole);
	ASSERT_FALSE(AtomHasRole(string, stringForm, stringRole))

	// restore role
	AtomAddRole(string, stringForm, stringRole);
	ASSERT_TRUE(AtomHasRole(string, stringForm, stringRole))

	// add 1 occurence of role
	AtomAddRole(string, stringForm, stringRole);
	ASSERT_TRUE(AtomHasRole(string, stringForm, stringRole))

	// remove both occurences
	LookupRemoveAllRoles(string);
	ASSERT_FALSE(AtomHasRole(string, stringForm, stringRole))

	ReleaseAtom(string);
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
		ASSERT_TRUE(LookupIteratorHasRecord(&iterator))
		Atom role = LookupIteratorGetRole(&iterator);
		// the role is either list or string
		ASSERT_TRUE(
			SameAtoms(role, GetCoreRoleName(ROLE_LIST)) ||
			SameAtoms(role, GetCoreRoleName(ROLE_STRING))
		)
		LookupIteratorNext(&iterator);
	}
	ASSERT_FALSE(LookupIteratorHasRecord(&iterator))
	FreeLookupIterator(&iterator);

	ReleaseAtom(string);
}


void testRemoveAllPredicateRoles(void)
{
	// create some DT_ID atoms
	Atom foo = CreateStringFromCString("foo");
	Atom bar = CreateStringFromCString("bar");
	Atom baz = CreateStringFromCString("baz");
	
	// create a new relation and assert some facts
	Atom foobar = CreateNameFromCString("foobar");
	Atom barf = CreateNameFromCString("barf");
	Atom form = CreatePredicateForm((Atom []) {foobar, barf}, 2);
	BTree * tree = RegistryCreateTable(form);

	AssertFact(form, (Atom[]) {foo, bar});
	ASSERT_TRUE(AtomHasRole(foo, form, foobar))
	ASSERT_TRUE(AtomHasRole(bar, form, barf))

	AssertFact(form, (Atom[]) {bar, baz});
	ASSERT_TRUE(AtomHasRole(bar, form, foobar))
	ASSERT_TRUE(AtomHasRole(baz, form, barf))

	AssertFact(form, (Atom[]) {foo, foo});
	ASSERT_TRUE(AtomHasRole(foo, form, barf))

	LookupRemoveAllPredicateRoles(form);

	// all associations with the form should now be removed.
	ASSERT_FALSE(AtomHasRole(foo, form, invalidAtom))
	ASSERT_FALSE(AtomHasRole(bar, form, invalidAtom))
	ASSERT_FALSE(AtomHasRole(baz, form, invalidAtom))

	// remove corresponding relation table rows
	RelationBTreeRemoveTuples(tree, 0, REMOVE_NORMAL);

	ReleaseAtom(form);
	ReleaseAtom(foo);
	ReleaseAtom(bar);
	ReleaseAtom(baz);
	ReleaseAtom(foobar);
	ReleaseAtom(barf);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testLookup);
	ExecuteTest(testLookupIterator);
	ExecuteTest(testRemoveAllPredicateRoles);

	KernelShutdown();

	TestSummary();
}

