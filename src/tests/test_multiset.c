
#include "kernel/UInt.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
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

	Atom multiset = CreateMultisetFromArrays(elements, multiples, TEST_MULTISET_N_UNIQUE, AT_LETTER);

	// we should have 3 tuples added to the table
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), initialNRows + 3)

	ASSERT_TRUE(IsMultiset(multiset))

	// multiset size
	ASSERT_UINT32_EQUAL(MultisetNUniqueElements(multiset), TEST_MULTISET_N_UNIQUE)
	ASSERT_UINT32_EQUAL(MultisetSize(multiset), TEST_MULTISET_SIZE)

	// iteration order of multiset currently yields elements ordered by multiple
	MultisetIterator iterator;
	MultisetIterate(multiset, &iterator);
		for(index32 i = 0; i < TEST_MULTISET_N_UNIQUE; i++) {
		ASSERT_TRUE(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		ASSERT_DATA64_EQUAL(em.element.hash, elements[i].hash)
		ASSERT_UINT32_EQUAL(em.multiple, multiples[i])
	}
	ASSERT_FALSE(MultisetIteratorNext(&iterator))
	MultisetIteratorEnd(&iterator);
 
	// creating again from the same elements should yield the same atom, with one additional reference
	Atom multiset2 = CreateMultisetFromArrays(elements, multiples, TEST_MULTISET_N_UNIQUE, AT_LETTER);
	ASSERT_DATA64_EQUAL(multiset.hash, multiset2.hash)
	IFactRelease(multiset2);

	// creating from permuted elements should yield the same multiset
	Atom permutedElements[3] = { three, one, two };
	size32 permutedMultiples[] = {3, 1, 2};

	Atom multiset3 = CreateMultisetFromArrays(permutedElements, permutedMultiples, TEST_MULTISET_N_UNIQUE, AT_LETTER);

	ASSERT_DATA64_EQUAL(multiset.hash, multiset3.hash)
	IFactRelease(multiset3);

	// attempt to remove any tuple (list @string position _ element _) should fail
	Atom tuple2[3];
	CoreFormSetTuple(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(Atom[]) { multiset, two, (Atom) {._uint = 2}},
		tuple2
	);
	ASSERT_UINT32_EQUAL(RelationTableRemoveTuple(table, tuple2, 0), TUPLE_PROTECTED)

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


