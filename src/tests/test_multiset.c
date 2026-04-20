
#include "kernel/UInt.h"
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
	
	TypedAtom elements[] = {
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
		ASSERT_TRUE(SameTypedAtoms(em.element, elements[i]))
		ASSERT_UINT32_EQUAL(em.multiple, multiples[i])
		MultisetIteratorNext(&iterator);
	}
	ASSERT_FALSE(MultisetIteratorHasNext(&iterator))
	MultisetIteratorEnd(&iterator);
 
	// creating again from the same elements should yield the same atom, with one additional reference
	Atom multiset2 = CreateMultisetFromArrays(elements, multiples, TEST_MULTISET_N_UNIQUE);
	ASSERT_DATA64_EQUAL(multiset, multiset2)
	IFactRelease(multiset2);

	// creating from permuted elements should yield the same multiset
	TypedAtom permutedElements[] = {
		GetAlphabetLetter('C'),
		GetAlphabetLetter('A'),
		GetAlphabetLetter('B')
	};
	size32 permutedMultiples[] = {3, 1, 2};

	Atom multiset3 = CreateMultisetFromArrays(permutedElements, permutedMultiples, TEST_MULTISET_N_UNIQUE);

	ASSERT_DATA64_EQUAL(multiset, multiset3)
	IFactRelease(multiset3);

	// adding a tuple (multiset @multiset element 'D' multiple 1) should fail
	// since @multiset is an ifact
	TypedAtom tuple1[3];
	MultisetSetTuple(tuple1, CreateTypedAtom(AT_ID, multiset), GetAlphabetLetter('D'), CreateUInt(1));
	ASSERT_UINT32_EQUAL(RelationBTreeAddTuple(table, tuple1), TUPLE_PROTECTED)

	// attempt to remove any tuple (list @string position _ element _) should fail
	TypedAtom tuple2[3];
	MultisetSetTuple(tuple2, CreateTypedAtom(AT_ID, multiset), GetAlphabetLetter('B'), CreateUInt(2));
	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuples(table, tuple2, REMOVE_NORMAL), 0)

	IFactRelease(multiset);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testMultiset);
	
	KernelShutdown();

	TestSummary();
}


