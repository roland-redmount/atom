
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
	ServiceRecord record = RegistryFindUntypedService(form);
	ASSERT(!record.form.hash)

	// asserting the first fact should create the service
	Atom barf = CreateStringFromCString("barf");
	TypedTuple * actors1 = CreateTypedTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_ID, barf),
			CreateTypedAtom(AT_INT, (Atom) {._int = -1})
		},
		2
	);
	AssertFact(form, actors1);
	record = RegistryFindUntypedService(form);
	ASSERT(record.form.hash)
	ASSERT(record.service->type == SERVICE_MACHINE)
	BTree * btree = (BTree *) record.service->impl.machine.providerData;

	ASSERT_UINT32_EQUAL(RelationBTreeNRows(btree), 1)

	Atom baz = CreateStringFromCString("baz");
	TypedTuple * actors2 = CreateTypedTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_INT, (Atom) {._int = 42}),
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
	record = RegistryFindUntypedService(form);
	ASSERT(!record.form.hash)

	FreeTypedTuple(actors1);
	FreeTypedTuple(actors2);
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
