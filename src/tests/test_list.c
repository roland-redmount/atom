
#include "lang/Variable.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/letter.h"
#include "kernel/lookup.h"

#include "testing/testing.h"


// TODO: we need more test cases!
// Should have a fuzz test with large number of tuples

#define EXAMPLE_LIST_N_ELEMENTS		3

static void testCreateList(void)
{
	RelationTable const * listLength = GetCoreRelationTable(RELATION_LIST_LENGTH);
	RelationTable const * listPositionElement = GetCoreRelationTable(RELATION_LIST_LETTER);

	size32 listLengthNRowsInitial = RelationTableNRows(listLength);
	size32 listPositionElementNRowsInitial = RelationTableNRows(listPositionElement);
	
	// create list
	Atom listAtoms[EXAMPLE_LIST_N_ELEMENTS] = {
		GetAlphabetLetter('X'),
		GetAlphabetLetter('Y'),
		GetAlphabetLetter('Z')
	};	
	Atom list = CreateListFromArray(listAtoms, AT_LETTER, EXAMPLE_LIST_N_ELEMENTS);

	// test (list length) relation table
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLength),listLengthNRowsInitial + 1)
	// test (list position element) relation table
	ASSERT_UINT32_EQUAL(
		RelationTableNRows(listPositionElement),
		listPositionElementNRowsInitial + EXAMPLE_LIST_N_ELEMENTS
	)
	// test elements are as expected
	for(index8 i = 0; i < EXAMPLE_LIST_N_ELEMENTS; i++) {
		ASSERT_DATA64_EQUAL(ListGetElement(list, i+1).hash, listAtoms[i].hash)
	}
	// test list length
	ASSERT_UINT32_EQUAL(ListLength(list), EXAMPLE_LIST_N_ELEMENTS)

	// test list iteration
	ListIterator iterator;
	ListIterate(list, &iterator);
	for(index8 i = 0; i < EXAMPLE_LIST_N_ELEMENTS; i++) {
		ASSERT_TRUE(ListIteratorNext(&iterator))
		Atom element = ListIteratorGetElement(&iterator);
		ASSERT_DATA64_EQUAL(element.hash, listAtoms[i].hash)
	}
	ASSERT_FALSE(ListIteratorNext(&iterator))
	ListIteratorEnd(&iterator);

	// test ListGetPosition
	// TODO: this is currently not supported; see ListGetPosition()
	// for(index8 i = 0; i < EXAMPLE_LIST_N_ELEMENTS; i++)
	// 	ASSERT_UINT32_EQUAL(ListGetPosition(list, listAtoms[i]), i + 1)

	IFactRelease(list);
}


#define NESTED_LIST_N_ELEMENTS	2

static void testNestedList(void)
{
	Atom innerListAtoms[EXAMPLE_LIST_N_ELEMENTS] = {
		GetAlphabetLetter('X'),
		GetAlphabetLetter('Y'),
		GetAlphabetLetter('Z')
	};
	Atom innerList = CreateListFromArray(innerListAtoms, AT_LETTER, EXAMPLE_LIST_N_ELEMENTS);

	Atom outerListAtoms[1] = {innerList};
	Atom outerList = CreateListFromArray(outerListAtoms, AT_ID, 1);
	ASSERT_INT32_EQUAL(IFactReferenceCount(innerList), 2)
	IFactRelease(innerList);
	
	// test ListGetElement
	ASSERT_DATA64_EQUAL(ListGetElement(outerList, 1).hash, outerListAtoms[0].hash)

	// test ListGetPosition
	// TODO: this is currently not supported; see ListGetPosition()
	// ASSERT_UINT32_EQUAL(ListGetPosition(outerList, innerList), 1)

	// NOTE: this will trigger release of the outerList atom.
	IFactRelease(outerList);
}


static void testCreateEmptyList(void)
{
	Atom emptyList = CreateListFromArray(0, AT_LETTER, 0);
	ASSERT_TRUE(IsList(emptyList))
	ASSERT_UINT32_EQUAL(ListLength(emptyList), 0)

	ListIterator iterator;
	ListIterate(emptyList, &iterator);
	ASSERT_FALSE(ListIteratorNext(&iterator))
	ListIteratorEnd(&iterator);

	IFactRelease(emptyList);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testCreateList);
	ExecuteTest(testNestedList);
	ExecuteTest(testCreateEmptyList);

	KernelShutdown();

	TestSummary();
}

