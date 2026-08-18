
#include "kernel/Int.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "kernel/RelationRegistry.h"
#include "kernel/string.h"
#include "lang/TermForm.h"
#include "memory/allocator.h"
#include "testing/fixtures.h"
#include "testing/testing.h"


void testAssertRetract(void)
{
	// term form (foo bar)
	byte atomTypes[2] = {AT_ID, AT_INT};
	Atom form = CreateTermFormFromRoleNames((char const * []) {"foo", "bar"}, 2, true);

	// check that relation table does not already exist
	ASSERT_NULL(RelationRegistryFind(form, 2, atomTypes))
	
	// asserting the first fact should create the service
	Atom barf = CreateStringFromCString("barf");
	TypedTuple * actors1 = CreateTypedTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_ID, barf),
			CreateTypedAtom(AT_INT, (Atom) {._int = -1})
		},
		2
	);
	AssertFact(form, actors1, 0);

	RelationTable const * relation = RelationRegistryFind(form, 2, atomTypes);
	ASSERT_NOT_NULL(relation)
	ASSERT_UINT32_EQUAL(RelationTableNRows(relation), 1)

	Atom baz = CreateStringFromCString("baz");
	TypedTuple * actors2 = CreateTypedTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_INT, (Atom) {._int = 42}),
			CreateTypedAtom(AT_ID, baz)
		},
		2
	);
	AssertFact(form, actors2, 0);
	ASSERT_UINT32_EQUAL(RelationTableNRows(relation), 2)

	RetractFact(form, actors2);
	ASSERT_UINT32_EQUAL(RelationTableNRows(relation), 1)

	// retracting the last fact should remove the service
	RetractFact(form, actors1);
	ASSERT_NULL(RelationRegistryFind(form, 2, atomTypes))
	
	FreeTypedTuple(actors1);
	FreeTypedTuple(actors2);
	IFactRelease(barf);
	IFactRelease(baz);
	IFactRelease(form);
}


int main(int argc, char * argv[])
{
#ifdef DEBUG_ALLOCATE
	SetAllocationLogging(true);
#endif

	KernelInitialize();

	// ExecuteTest(testAssertRetract);

	KernelShutdown();

	TestSummary();
}
