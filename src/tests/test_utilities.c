

#include "kernel/kernel.h"
#include "memory/paging.h"
#include "memory/allocator.h"
#include "util/LinkedList.h"
#include "util/resources.h"
#include "util/ResizingBuffer.h"
#include "util/ResizingArray.h"
#include "util/sort.h"
#include "util/utilities.h"
#include "testing/testing.h"


void testResizingArray(void)
{
	// make a ResizingArray storing 5-byte strings as elements
	char const * strings[] = {
		"abcde", "fghij", "klmno", "pqrst"
	};
	const size32 stringLength = 5;
	size32 nStrings = 4;
	
	const size32 initialCapacity = 2;
	ResizingArray array;
	CreateResizingArray(&array, stringLength, initialCapacity);
	ASSERT_UINT32_EQUAL(ResizingArrayNElements(&array), 0)

	for(index32 i = 0; i < nStrings; i++) {
		ResizingArrayAppend(&array, strings[i]);
		ASSERT_UINT32_EQUAL(ResizingArrayNElements(&array), i + 1)
		char const * arrayElement = ResizingArrayGetElement(&array, i);
		ASSERT_MEMORY_EQUAL(arrayElement, strings[i], stringLength)
	}

	// convert to contiguous array
	char const * arrayBytes = ResizingArrayGetMemory(&array);
	ASSERT_MEMORY_EQUAL(
		arrayBytes,
		"abcdefghijklmnopqrst",
		nStrings * stringLength 
	)
	FreeResizingArray(&array);
}


static bool stringComparator(const void* string1, const void* string2)
{
	return CStringCompare(string1, string2) == 0;
}


void testLinkedList(void)
{
	char const * item1 = "one";
	LinkedList * list1 = CreateLinkedList((void *) item1);
	ASSERT_PTR_EQUAL(GetLinkedListItem(list1), item1)
	ASSERT_TRUE(!LinkedListHasNext(list1))
	ASSERT_PTR_EQUAL(GetNextLinkedList(list1), 0)

	// append an element
	char const * item2 = "two";
	LinkedList* list2 = AppendToLinkedList(&list1, item2);
	ASSERT_TRUE(LinkedListHasNext(list1))
	ASSERT_PTR_EQUAL(GetNextLinkedList(list1), list2)
	ASSERT_PTR_EQUAL(GetLinkedListItem(list2), item2)
	ASSERT_TRUE(!LinkedListHasNext(list2))
	ASSERT_PTR_EQUAL(GetNextLinkedList(list2), 0)

	// find an element
	char const * sameAsItem2 = "two";
	LinkedList** findResult = FindLinkedListItem(&list1, sameAsItem2, stringComparator);
	ASSERT_PTR_EQUAL(*findResult, list2)

	char const * notPresentItem = "three";
	findResult = FindLinkedListItem(&list1, notPresentItem, stringComparator);
	ASSERT_PTR_EQUAL(*findResult, 0)

	FreeLinkedList(list1, 0);	
}


void testResources(void)
{
	char relResourcePath[maxPathLength + 1];
	GetResourceDirectory(relResourcePath, maxPathLength);
	// TODO: test that string ends with /tests/resources ?
	ASSERT_PTR_NOT_EQUAL(relResourcePath, 0)
}


void testReorderArray(void)
{
	char data[9] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};
	size8 const itemSize = 3;
	index8 ordering[3] = {2, 0, 1};
	ReorderArray((byte *) data, ordering, 3, itemSize);
	char const reorderedData[9] = {'g', 'h', 'i', 'a', 'b', 'c', 'd', 'e', 'f'};
	ASSERT_MEMORY_EQUAL(reorderedData, data, 9)
}


void testReorderRaggedArray(void)
{
	char data[9] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};
	size32 blockSizes[9] = {3, 4, 2};
	index8 ordering[3] = {2, 0, 1};
	ReorderRaggedArray((byte *) data, ordering, blockSizes, 3);
	char const reorderedData[9] = {'h', 'i', 'a', 'b', 'c', 'd', 'e', 'f', 'g'};
	ASSERT_MEMORY_EQUAL(reorderedData, data, 9)
}


void testQuickSort(void)
{
	char const * unsorted_items = "fcjdebklgiha";
	char items[12];

	// sort single characters
	CopyMemory(unsorted_items, items, 12);
	QuickSort(items, 12, 1, 0);
	// printf("sorted items = %.12s\n", items);
	ASSERT_MEMORY_EQUAL(items, "abcdefghijkl", 12)

	// sort 2-char blocks
	CopyMemory(unsorted_items, items, 12);
	QuickSort(items, 6, 2, 0);
	// printf("sorted items = %.12s\n", items);
	ASSERT_MEMORY_EQUAL(items, "ebfcgihajdkl", 12)

	// sort 3-char blocks
	CopyMemory(unsorted_items, items, 12);
	QuickSort(items, 4, 3, 0);
	// printf("sorted items = %.12s\n", items);
	ASSERT_MEMORY_EQUAL(items, "debfcjihaklg", 12)

	// TODO: generate some random data and test our implementation
	// against stdlib qsort()

}


#define TEST_BUFFER_CONTENT_SIZE	16

void testResizingBuffer(void)
{
	byte content[TEST_BUFFER_CONTENT_SIZE];
	for(index32 i = 0; i < TEST_BUFFER_CONTENT_SIZE; i++)
		content[i] = i;

	ResizingBuffer buffer;
	CreateResizingBuffer(&buffer, TEST_BUFFER_CONTENT_SIZE);
	ASSERT_UINT32_EQUAL(GetBufferSize(&buffer), 0)

	AppendToBuffer(&buffer, content, TEST_BUFFER_CONTENT_SIZE);
	ASSERT_UINT32_EQUAL(GetBufferSize(&buffer), TEST_BUFFER_CONTENT_SIZE)
	void * bufferContent = GetBufferMemBlock(&buffer);
	ASSERT_MEMORY_EQUAL(bufferContent, content, TEST_BUFFER_CONTENT_SIZE)

	FreeResizingBuffer(&buffer);
}


int main(int argc, char * argv[])
{
	SetupMemory();

	testResizingArray();
	testLinkedList();
	testResources();
	testReorderArray();
	testReorderRaggedArray();
	testQuickSort();
	testResizingBuffer();

	CleanupMemory();

	TestSummary();
}
