
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
	Datum roles[2] = {CreateNameFromCString("foo"), CreateNameFromCString("bar")};
	Datum form = CreatePredicateForm(roles, 2);
	NameRelease(roles[0]);
	NameRelease(roles[1]);
	
	Service service = RegistryFindService(form);
	ASSERT(service.type == SERVICE_NONE)

	Atom actors1[2] = {
		CreateAtom(DT_ID, CreateStringFromCString("barf")),
		CreateInt(-1)
	};
	AssertFact(form, actors1);
	service = RegistryFindService(form);
	ASSERT(service.type == SERVICE_BTREE)
	BTree * btree = service.service.tree;
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(btree), 1)

	Atom actors2[] = {
		CreateInt(42),
		CreateAtom(DT_ID, CreateStringFromCString("baz"))
	};
	AssertFact(form, actors2);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(btree), 2)

	RetractFact(form, actors2);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(btree), 1)

	RetractFact(form, actors1);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(btree), 0)

	ReleaseAtom(actors1[0]);
	ReleaseAtom(actors2[1]);
	IFactRelease(form);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testAssertRetract);

	KernelShutdown();

	TestSummary();
}
