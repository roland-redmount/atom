

#include "datumtypes/UInt.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/pair.h"
#include "testing/testing.h"


static void testPair(void)
{
	Atom form = GetCorePredicateForm(FORM_PAIR_LEFT_RIGHT);
	BTree * pairTable = RegistryLookupTable(form);
	uint32 initialNRows = RelationBTreeNRows(pairTable);

	// create a pair
	Atom left = GetAlphabetLetter('x');
	Atom right = CreateUInt(42);
	Atom pair1 = CreatePair(left, right);
	
	ASSERT_UINT32_EQUAL(pair1.type, DT_ID)
	ASSERT_TRUE(IsPair(pair1))

	ASSERT_UINT32_EQUAL(RelationBTreeNRows(pairTable), initialNRows + 1)

	ASSERT_TRUE(SameAtoms(PairGetElement(pair1, PAIR_LEFT), left))
	ASSERT_TRUE(SameAtoms(PairGetElement(pair1, PAIR_RIGHT), right))


	// attempt to add the same pair again
	Atom pair2 = CreatePair(left, right);
	ASSERT_TRUE(SameAtoms(pair1, pair2))
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(pairTable), initialNRows + 1)
	ReleaseAtom(pair2);
	
	// a pair containing another pair
	Atom pair3 = CreatePair(pair1, right);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(pairTable), initialNRows + 2)

	ASSERT_TRUE(SameAtoms(PairGetElement(pair3, PAIR_LEFT), pair1))
	ASSERT_TRUE(SameAtoms(PairGetElement(pair3, PAIR_RIGHT), right))
	
	ReleaseAtom(pair3);
	ReleaseAtom(pair1);

	ASSERT_UINT32_EQUAL(RelationBTreeNRows(pairTable), initialNRows)
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testPair);

	KernelShutdown();

	TestSummary();
}

