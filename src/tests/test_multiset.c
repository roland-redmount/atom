
#include "kernel/UInt.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "lang/name.h"
#include "testing/testing.h"

#define TEST_MULTISET_N_UNIQUE	3
#define TEST_MULTISET_SIZE		6


static void testMultiset(void)
{
	RelationTable const * table = GetCoreRelationTable(RELATION_MULTISET_NAME);
	uint32 initialNRows = RelationTableNRows(table);

	Atom one = CreateNameFromCString("one");
	Atom two = CreateNameFromCString("two");
	Atom three = CreateNameFromCString("three");

	Atom elements[3] = {one, two, three};
	size32 multiples[] = {1, 2, 3};

	Atom multiset = CreateMultisetFromArrays(elements, multiples, TEST_MULTISET_N_UNIQUE, AT_NAME);

	// we should have 3 tuples added to the table
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), initialNRows + 3)

	ASSERT_TRUE(IsMultiset(multiset))

	// multiset size
	ASSERT_UINT32_EQUAL(MultisetNUniqueElements(multiset, AT_NAME), TEST_MULTISET_N_UNIQUE)
	ASSERT_UINT32_EQUAL(MultisetSize(multiset, AT_NAME), TEST_MULTISET_SIZE)

	// iteration order of multiset currently yields elements ordered by multiple
	MultisetIterator iterator;
	MultisetIterate(multiset, AT_NAME, &iterator);
		for(index32 i = 0; i < TEST_MULTISET_N_UNIQUE; i++) {
		ASSERT_TRUE(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		ASSERT_DATA64_EQUAL(em.element.hash, elements[i].hash)
		ASSERT_UINT32_EQUAL(em.multiple, multiples[i])
	}
	ASSERT_FALSE(MultisetIteratorNext(&iterator))
	MultisetIteratorEnd(&iterator);

	// the multiple of each element, and zero for an element the multiset does not hold
	for(index32 i = 0; i < TEST_MULTISET_N_UNIQUE; i++)
		ASSERT_UINT32_EQUAL(MultisetGetElementMultiple(multiset, elements[i]), multiples[i])
	Atom four = CreateNameFromCString("four");
	ASSERT_UINT32_EQUAL(MultisetGetElementMultiple(multiset, four), 0)
	NameRelease(four);

	// creating again from the same elements should yield the same atom, with one additional reference
	Atom multiset2 = CreateMultisetFromArrays(elements, multiples, TEST_MULTISET_N_UNIQUE, AT_NAME);
	ASSERT_DATA64_EQUAL(multiset.hash, multiset2.hash)
	IFactRelease(multiset2);

	// creating from permuted elements should yield the same multiset
	Atom permutedElements[3] = { three, one, two };
	size32 permutedMultiples[] = {3, 1, 2};

	Atom multiset3 = CreateMultisetFromArrays(permutedElements, permutedMultiples, TEST_MULTISET_N_UNIQUE, AT_NAME);

	ASSERT_DATA64_EQUAL(multiset.hash, multiset3.hash)
	IFactRelease(multiset3);

	IFactRelease(multiset);
	NameRelease(one);
	NameRelease(two);
	NameRelease(three);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testMultiset);
	
	KernelShutdown();

	TestSummary();
}


