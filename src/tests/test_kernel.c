
#include "datumtypes/Int.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "lang/name.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "lang/PredicateForm.h"
#include "testing/testing.h"


void testAssertRetract(void)
{
	// form (foo bar)
	Atom roles[2] = {CreateNameFromCString("foo"), CreateNameFromCString("bar")};
	Atom form = CreatePredicateForm(roles, 2);
	NameRelease(roles[0]);
	NameRelease(roles[1]);
	
	// check that service does not already exist
	ServiceRecord record = RegistryFindBTreeService(form);
	ASSERT(record.type == SERVICE_NONE)

	// asserting the first fact should create the service
	TypedAtom actors1[2] = {
		CreateTypedAtom(AT_ID, CreateStringFromCString("barf")),
		CreateInt(-1)
	};
	AssertFact(form, actors1);
	record = RegistryFindBTreeService(form);
	ASSERT(record.type == SERVICE_BTREE)
	BTree * btree = record.provider.tree;
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(btree), 1)

	TypedAtom actors2[] = {
		CreateInt(42),
		CreateTypedAtom(AT_ID, CreateStringFromCString("baz"))
	};
	AssertFact(form, actors2);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(btree), 2)

	RetractFact(form, actors2);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(btree), 1)

	// retracting the last fact should remove the service
	RetractFact(form, actors1);
	record = RegistryFindBTreeService(form);
	ASSERT(record.type == SERVICE_NONE)

	ReleaseTypedAtom(actors1[0]);
	ReleaseTypedAtom(actors2[1]);
	IFactRelease(form);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testAssertRetract);

	KernelShutdown();

	TestSummary();
}
