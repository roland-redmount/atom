
#include "kernel/Int.h"
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
	Atom barf = CreateStringFromCString("barf");
	Tuple * actors1 = CreateTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_ID, barf),
			CreateTypedAtom(AT_INT, -1)
		},
		2
	);
	AssertFact(form, actors1);
	record = RegistryFindBTreeService(form);
	ASSERT(record.type == SERVICE_BTREE)
	BTree * btree = record.provider.tree;

	ASSERT_UINT32_EQUAL(RelationBTreeNRows(btree), 1)

	Atom baz = CreateStringFromCString("baz");
	Tuple * actors2 = CreateTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_INT, 42),
			CreateTypedAtom(AT_ID, baz)
		},
		2
	);
	AssertFact(form, actors2);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(btree), 2)

	RetractFact(form, actors2);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(btree), 1)

	// retracting the last fact should remove the service
	RetractFact(form, actors1);
	record = RegistryFindBTreeService(form);
	ASSERT(record.type == SERVICE_NONE)

	FreeTuple(actors1);
	FreeTuple(actors2);
	IFactRelease(barf);
	IFactRelease(baz);
	IFactRelease(form);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testAssertRetract);

	KernelShutdown();

	TestSummary();
}
