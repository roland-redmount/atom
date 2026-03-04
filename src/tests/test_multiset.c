
#include "datumtypes/UInt.h"
#include "kernel/letter.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "testing/testing.h"

#define TEST_MULTISET_N_UNIQUE	3
#define TEST_MULTISET_SIZE		6


static void testMultiset(void)
{
	BTree * table = RegistryGetCoreTable(FORM_MULTISET_ELEMENT_MULTIPLE);
	uint32 initialNRows = RelationBTreeNRows(table);
	
	Atom elements[] = {
		GetAlphabetLetter('A'),
		GetAlphabetLetter('B'),
		GetAlphabetLetter('C')
	};
	size32 multiples[] = {1, 2, 3};

	Atom multiset = CreateMultisetFromArrays(elements, multiples, TEST_MULTISET_N_UNIQUE);

	// we should have 3 tuples added to the table
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(table), initialNRows + 3)

	ASSERT_TRUE(IsMultiset(multiset))

	// multiset size
	ASSERT_UINT32_EQUAL(MultisetNUniqueElements(multiset), TEST_MULTISET_N_UNIQUE)
	ASSERT_UINT32_EQUAL(MultisetSize(multiset), TEST_MULTISET_SIZE)

	// iteration order of multiset yields elements ordered by multiple
	MultisetIterator iterator;
	MultisetIterate(multiset, &iterator);
	
	for(index32 i = 0; i < TEST_MULTISET_N_UNIQUE; i++) {
		ASSERT_TRUE(MultisetIteratorHasNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		ASSERT_TRUE(SameAtoms(em.element, elements[i]))
		ASSERT_UINT32_EQUAL(em.multiple, multiples[i])
		MultisetIteratorNext(&iterator);
	}
	ASSERT_FALSE(MultisetIteratorHasNext(&iterator))
	MultisetIteratorEnd(&iterator);
 
	// creating again from the same elements should yield the same atom, with one additional reference
	Atom multiset2 = CreateMultisetFromArrays(elements, multiples, TEST_MULTISET_N_UNIQUE);
	ASSERT_TRUE(SameAtoms(multiset, multiset2))
	ReleaseAtom(multiset2);

	// creating from permuted elements should yield the same multiset
	Atom permutedElements[] = {
		GetAlphabetLetter('C'),
		GetAlphabetLetter('A'),
		GetAlphabetLetter('B')
	};
	size32 permutedMultiples[] = {3, 1, 2};

	Atom multiset3 = CreateMultisetFromArrays(permutedElements, permutedMultiples, TEST_MULTISET_N_UNIQUE);

	ASSERT_TRUE(SameAtoms(multiset, multiset3))
	ReleaseAtom(multiset3);

	// asserting a fact (multiset @multiset element 'D' multiple 1) should fail
	// TODO: this fails now
	Atom tuple1[3];
	MultisetSetTuple(tuple1, multiset, GetAlphabetLetter('D'), CreateUInt(1));
	ASSERT_UINT32_EQUAL(RelationBTreeAddTuple(table, tuple1), TUPLE_PROTECTED)

	// attempt to remove any tuple (list @string position _ element _) should fail
	Atom tuple2[3];
	MultisetSetTuple(tuple2, multiset, GetAlphabetLetter('B'), CreateUInt(2));
	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuples(table, tuple2, REMOVE_NORMAL), 0)

	ReleaseAtom(multiset);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testMultiset);
	
	KernelShutdown();

	TestSummary();
}


