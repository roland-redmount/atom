
#include "datumtypes/UInt.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/pair.h"
#include "testing/testing.h"


static void testPair(void)
{
	Atom form = GetCorePredicateForm(FORM_PAIR_LEFT_RIGHT);
	Service service = RegistryFindBTreeService(form);
	ASSERT(service.type == SERVICE_BTREE)
	BTree * pairTable = service.service.tree;
	uint32 initialNRows = RelationBTreeNRows(pairTable);

	// create a pair
	TypedAtom left = GetAlphabetLetter('x');
	TypedAtom right = CreateUInt(42);
	Atom pair1 = CreatePair(left, right);
	
	ASSERT_TRUE(IsPair(pair1))

	ASSERT_UINT32_EQUAL(RelationBTreeNRows(pairTable), initialNRows + 1)

	ASSERT_TRUE(SameTypedAtoms(PairGetElement(pair1, PAIR_LEFT), left))
	ASSERT_TRUE(SameTypedAtoms(PairGetElement(pair1, PAIR_RIGHT), right))


	// attempt to add the same pair again
	Atom pair2 = CreatePair(left, right);
	ASSERT_DATA64_EQUAL(pair1, pair2)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(pairTable), initialNRows + 1)
	IFactRelease(pair2);
	
	// a pair containing another pair
	Atom pair3 = CreatePair(CreateTypedAtom(AT_ID, pair1), right);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(pairTable), initialNRows + 2)

	ASSERT_TRUE(SameTypedAtoms(PairGetElement(pair3, PAIR_LEFT), CreateTypedAtom(AT_ID, pair1)))
	ASSERT_TRUE(SameTypedAtoms(PairGetElement(pair3, PAIR_RIGHT), right))
	
	IFactRelease(pair3);
	IFactRelease(pair1);

	ASSERT_UINT32_EQUAL(RelationBTreeNRows(pairTable), initialNRows)
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testPair);

	KernelShutdown();

	TestSummary();
}

