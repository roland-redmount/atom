
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
	BTree * table = RegistryGetCoreBTreeService(FORM_MULTISET_ELEMENT_MULTIPLE);
	uint32 initialNRows = RelationBTreeNRows(table);
	
	TypedAtom elements[] = {
		CreateTypedAtom(AT_LETTER, GetAlphabetLetter('A')),
		CreateTypedAtom(AT_LETTER, GetAlphabetLetter('B')),
		CreateTypedAtom(AT_LETTER, GetAlphabetLetter('C'))
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
		ASSERT_TRUE(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		ASSERT_TRUE(SameTypedAtoms(em.element, elements[i]))
		ASSERT_UINT32_EQUAL(em.multiple, multiples[i])
	}
	ASSERT_FALSE(MultisetIteratorNext(&iterator))
	MultisetIteratorEnd(&iterator);
 
	// creating again from the same elements should yield the same atom, with one additional reference
	Atom multiset2 = CreateMultisetFromArrays(elements, multiples, TEST_MULTISET_N_UNIQUE);
	ASSERT_DATA64_EQUAL(multiset.hash, multiset2.hash)
	IFactRelease(multiset2);

	// creating from permuted elements should yield the same multiset
	TypedAtom permutedElements[] = {
		CreateTypedAtom(AT_LETTER, GetAlphabetLetter('C')),
		CreateTypedAtom(AT_LETTER, GetAlphabetLetter('A')),
		CreateTypedAtom(AT_LETTER, GetAlphabetLetter('B'))
	};
	size32 permutedMultiples[] = {3, 1, 2};

	Atom multiset3 = CreateMultisetFromArrays(permutedElements, permutedMultiples, TEST_MULTISET_N_UNIQUE);

	ASSERT_DATA64_EQUAL(multiset.hash, multiset3.hash)
	IFactRelease(multiset3);

	// adding a tuple (multiset @multiset element 'D' multiple 1) should fail
	// since @multiset is an ifact
	TypedTuple * tuple1 = CreateTypedTuple(3);
	MultisetSetTuple(
		tuple1,
		CreateTypedAtom(AT_ID, multiset),
		CreateTypedAtom(AT_LETTER, GetAlphabetLetter('D')),
		CreateTypedAtom(AT_UINT, (Atom) {._uint = 1})
	);
	ASSERT_UINT32_EQUAL(RelationBTreeAddTuple(table, tuple1), TUPLE_PROTECTED)
	FreeTypedTuple(tuple1);

	// attempt to remove any tuple (list @string position _ element _) should fail
	TypedTuple * tuple2 = CreateTypedTuple(3);
	MultisetSetTuple(
		tuple2,
		CreateTypedAtom(AT_ID, multiset),
		CreateTypedAtom(AT_LETTER, GetAlphabetLetter('B')),
		CreateTypedAtom(AT_UINT, (Atom) {._uint = 2})
	);
	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuples(table, tuple2, REMOVE_NORMAL), 0)
	FreeTypedTuple(tuple2);

	IFactRelease(multiset);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testMultiset);
	
	KernelShutdown();

	TestSummary();
}


