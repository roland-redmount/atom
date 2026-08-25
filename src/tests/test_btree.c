#include "btree/btree.h"
#include "kernel/kernel.h"
#include "memory/allocator.h"
#include "testing/testing.h"
#include "util/sort.h"


// Item structure to use. B-tree item size can be set larger
// than sizeof(TestItem) to control branching factor of the b-tree,
// given that node size is constant.

typedef struct {
	// NOTE: we need to accomodate negative integer keys in some test cases.
	int32 key;
	uint32 value;
} TestItem;

static void initTestItem(TestItem * item, int key)
{
	item->key = key;
	item->value = RandomInteger(0, 1e6);
}

// Compare two test item structures by key
static int8 compareTestItems(void const * item1, void const * item2, size32 itemSize)
{
    int key1 = ((TestItem *) item1)->key;
	int key2 = ((TestItem *) item2)->key;
	if(key1 < key2)
		return -1;
	else if(key1 > key2)
		return 1;
	else
		return 0;
}

// A comparator reading the item key as two 16-bit parts, where a zero second part
// of a key is ignored, so that such a key matches every item with the same first part.
// This is similar to prefix keys used in the kernel registries; see for example compareServices().
static int8 compareTestItemPrefix(void const * item, void const * itemOrKey, size32 itemSize)
{
	TestItem const * storedItem = item;
	TestItem const * key = itemOrKey;

	int16 itemFirst = storedItem->key >> 16;
	int16 keyFirst = key->key >> 16;
	if(itemFirst < keyFirst)
		return -1;
	else if(itemFirst > keyFirst)
		return 1;

	int16 keySecond = key->key & 0xFFFF;
	if(!keySecond)
		return 0;
	int16 itemSecond = storedItem->key & 0xFFFF;
	if(itemSecond < keySecond)
		return -1;
	else if(itemSecond > keySecond)
		return 1;
	else
		return 0;
}


void printTestItem(void const * item)
{
	TestItem const * testItem = item;
	PrintF("key = %u value = %u\n", testItem->key, testItem->value);
}

void printTestItemKey(void const * item)
{
	TestItem const * testItem = item;
	PrintF("%u", testItem->key);
}

// Test if two items are equal by content
static bool testItemsEqual(TestItem const * item, TestItem const * item2)
{
	return CompareMemory(item, item2, sizeof(TestItem)) == 0;
}


// NOTE: the B-tree verification functions only exist in debug builds,
// so these checks are simply skipped elsewhere
void validateBTree(BTree const * btree)
{
#ifdef DEBUG
    ASSERT_TRUE(BTreeIsBalanced(btree))
    ASSERT_TRUE(BTreeDeepCount(btree) == btree->nItemsTotal)
    ASSERT_TRUE(BTreeVerifyBounds(btree))
    ASSERT_TRUE(BTreeVerifyItemOrder(btree))
#endif
}

void printTestBTree(BTree const * btree)
{
#ifdef DEBUG
	BTreePrint(btree, printTestItemKey);
#endif
}


// --------------------------------- TESTS ----------------------------------


/**
 * Test on a small, deterministic example.
 */
void testBTreeExample(void)
{
	size32 nTestItems = 21;
	const size32 bTreeItemSize = 600;

	TestItem * items = Allocate(bTreeItemSize * (nTestItems + 1));
    for(index32 i = 0; i < nTestItems; i++)
		initTestItem(&items[i], i);
	TestItem * itemCopy = Allocate(bTreeItemSize);

	// start with an empty B-tree
	BTree * btree = BTreeCreate(bTreeItemSize, compareTestItems, 0);

	// insert items in order
	for(index32 i = 0; i < nTestItems; i++) {
		// insert the new item
		ASSERT_TRUE(BTreeInsert(btree, &items[i]) == BTREE_INSERTED)
		// check B-tree integrity
		ASSERT_UINT32_EQUAL(btree->nItemsTotal, i+1)
		validateBTree(btree);
	}
	// PrintF("btree item size = %u, storing N = %u nodes, node max items = %u\n",
	// 	btree->itemSize, nTestItems, btree->nodeMaxNItems);

	// delete items in order
	for(index32 i = 0; i < nTestItems; i++) {
		ASSERT_TRUE(BTreeDelete(btree, &items[i], itemCopy) == BTREE_DELETED)
		ASSERT_TRUE(testItemsEqual(&items[i], itemCopy))
		// check B-tree integrity
		ASSERT_UINT32_EQUAL(btree->nItemsTotal, nTestItems - i - 1)
		validateBTree(btree);
	}
	BTreeFree(btree);
	Free(itemCopy);
	Free(items);
}


/**
 * Test B-tree operations in both linear and random order
 */
void testBTreeRandomized(void)
{
	// Time complexity of this test is N log(N) in the number of items.
    size32 nTestItems = 200;
	const size32 bTreeItemSize = 400;

	TestItem * items = Allocate(bTreeItemSize * nTestItems);
    for(index32 i = 0; i < nTestItems; i++)
		initTestItem(&items[i], i);
	// an item of full btree item size
	TestItem * itemCopy = Allocate(bTreeItemSize);

	// start with an empty B-tree
	BTree * btree = BTreeCreate(bTreeItemSize, compareTestItems, 0);
	// PrintF("btree item size = %u, storing N = %u nodes, node max items = %u\n",
	// 	btree->itemSize, nTestItems, btree->nodeMaxNItems);

	// permute the item array
	RandomPermutation(items, nTestItems, sizeof(TestItem));

	for(index32 i = 0; i < nTestItems; i++) {
		// printTestItem(&items[i]);

		// item does not exist in tree
		ASSERT_FALSE(BTreeContainsItem(btree, &items[i]))
		// insert the new item
		ASSERT_TRUE(BTreeInsert(btree, &items[i]) == BTREE_INSERTED)
		// inserting item again
		ASSERT_TRUE(BTreeInsert(btree, &items[i]) == BTREE_UPDATED)
		// check B-tree integrity
		ASSERT_UINT32_EQUAL(btree->nItemsTotal, i+1)
		validateBTree(btree);

		// delete item
		ASSERT_TRUE(BTreeDelete(btree, &items[i], itemCopy) == BTREE_DELETED);
		ASSERT_TRUE(testItemsEqual(&items[i], itemCopy))
		ASSERT_UINT32_EQUAL(btree->nItemsTotal, i)
		validateBTree(btree);

		// attempt to delete item again
		ASSERT_TRUE(BTreeDelete(btree, &items[i], itemCopy) == BTREE_NO_MATCH);
		ASSERT_UINT32_EQUAL(btree->nItemsTotal, i)
		validateBTree(btree);

		// verify item is not present in tree
		ASSERT_FALSE(BTreeContainsItem(btree, &items[i]))

		// reinsert item
		ASSERT_TRUE(BTreeInsert(btree, &items[i]) == BTREE_INSERTED)
		ASSERT_UINT32_EQUAL(btree->nItemsTotal, i+1)
		validateBTree(btree);

		// retrieve item
		TestItem * item = BTreePeekItem(btree, &items[i]);
		ASSERT_NOT_NULL(item)
		ASSERT_TRUE(testItemsEqual(item, &items[i]))
	}
	// b-tree now contains all items

	// attempt to delete items outside the range of keys in the tree
	TestItem minItem = {.key = -1 };
	ASSERT_TRUE(BTreeDelete(btree, &minItem, 0) == BTREE_NO_MATCH)
	TestItem maxItem = {.key = nTestItems };
	ASSERT_TRUE(BTreeDelete(btree, &maxItem, 0) == BTREE_NO_MATCH)

    // delete all items in random order
    RandomPermutation(items, nTestItems, sizeof(TestItem));
    for(index32 i = 0; i < nTestItems; i++) {
		if(BTreeDelete(btree, &items[i], itemCopy) != BTREE_DELETED) {
			ASSERT(false)
		}
		ASSERT_TRUE(testItemsEqual(&items[i], itemCopy))
        validateBTree(btree);
    }

	// Tree is now empty.
    // Insert all items in random order
    RandomPermutation(items, nTestItems, sizeof(TestItem));
    // int min_key, max_key;
    for(index32 i = 0; i < nTestItems; i++) {
		ASSERT_TRUE(BTreeInsert(btree, &items[i]) == BTREE_INSERTED)
        validateBTree(btree);
    }
	ASSERT_UINT32_EQUAL(BTreeNItems(btree), nTestItems)

	// Replace items with a new set of items with same keys
	// but different content
    for(index32 i = 0; i < nTestItems; i++)
		initTestItem(&items[i], i);
	RandomPermutation(items, nTestItems, sizeof(TestItem));

	for(index32 i = 0; i < nTestItems; i++) {
		ASSERT_TRUE(BTreeInsert(btree, &items[i]) == BTREE_UPDATED)
		BTreeGetItem(btree, &items[i], itemCopy);
		ASSERT_TRUE(testItemsEqual(&items[i], itemCopy))
		validateBTree(btree);
	}
	ASSERT_UINT32_EQUAL(BTreeNItems(btree), nTestItems)

    // delete all items with BTreeClear()
	BTreeClear(btree);
    ASSERT_UINT32_EQUAL(btree->nItemsTotal, 0)
    validateBTree(btree);

	// insert items again
    RandomPermutation(items, nTestItems, sizeof(TestItem));
    for(uint32 i = 0; i < nTestItems; i++)
		ASSERT_TRUE(BTreeInsert(btree, &items[i]) == BTREE_INSERTED)

    // delete even-numbered keys
    for(uint32 i = 0; i < nTestItems; i += 2) {
		ASSERT_TRUE(BTreeDelete(btree, &items[i], itemCopy) == BTREE_DELETED)
		ASSERT_TRUE(testItemsEqual(&items[i], itemCopy))
		validateBTree(btree);
	}
    // even-numbered keys should should be absent
    for(index32 i = 0; i < nTestItems; i++) {
		if(i % 2)
        	ASSERT_TRUE(BTreeContainsItem(btree, &items[i]))
		else
			ASSERT_FALSE(BTreeContainsItem(btree, &items[i]))
    }
	ASSERT_UINT32_EQUAL(btree->nItemsTotal, nTestItems / 2)

    // delete odd-numbered keys
    for(uint32 i = 1; i < nTestItems; i += 2) {
		ASSERT_TRUE(BTreeDelete(btree, &items[i], itemCopy) == BTREE_DELETED)
		ASSERT_TRUE(testItemsEqual(&items[i], itemCopy))
		validateBTree(btree);
	}
    for(index32 i = 0; i < nTestItems; i++)
		ASSERT_FALSE(BTreeContainsItem(btree, &items[i]))

    BTreeFree(btree);
	Free(itemCopy);
    Free(items);
}


void testBTreeIterator(void)
{
    int nTestItems = 100;
	const size32 bTreeItemSize = 400;

	BTree * btree = BTreeCreate(bTreeItemSize, compareTestItems, 0);

	// iterate over empty tree
    BTreeIterator iterator;
	BTreeIterate(&iterator, btree);
	ASSERT_TRUE(BTreeIsWriteLocked(btree))

	ASSERT_FALSE(BTreeIteratorNext(&iterator))
	BTreeIteratorEnd(&iterator);
	ASSERT_FALSE(BTreeIsWriteLocked(btree))
	
	// insert a range of items into the tree
	TestItem * items = Allocate(bTreeItemSize * nTestItems);
    for(index32 i = 0; i < nTestItems; i++) {
		initTestItem(&items[i], i);
		ASSERT_TRUE(BTreeInsert(btree, &items[i]) == BTREE_INSERTED)
	}

	// iterate over tree
	BTreeIterate(&iterator, btree);
	ASSERT_TRUE(BTreeIsWriteLocked(btree))
	for(index32 i = 0; i < nTestItems; i++) {
		ASSERT_TRUE(BTreeIteratorNext(&iterator));
		TestItem const * item = BTreeIteratorPeekItem(&iterator);
		ASSERT_TRUE(testItemsEqual(&items[i], item))
	}
	ASSERT_FALSE(BTreeIteratorNext(&iterator))
	BTreeIteratorEnd(&iterator);
	ASSERT_FALSE(BTreeIsWriteLocked(btree))

	// test iterator seeking
	BTreeIterate(&iterator, btree);
	for(index32 k = 0; k < 100; k++) {
		// seek to a randomly chosen item
		index32 i = RandomInteger(0, nTestItems - 1);
		ASSERT_TRUE(BTreeIteratorSeek(&iterator, &items[i]))
		// iterate to end
		for(index32 j = i; j < nTestItems; j++) {
			TestItem const * item = BTreeIteratorPeekItem(&iterator);
			ASSERT_TRUE(testItemsEqual(&items[j], item))
			// all but the last item has a next item
			ASSERT_TRUE(BTreeIteratorNext(&iterator) == (j < nTestItems - 1))
		}
	}
	BTreeIteratorEnd(&iterator);

    BTreeFree(btree);
    Free(items);
}


/**
 * Test seeking with a prefix key, which several items compare equal to. In this case,
 * the B-tree must always return the first of all matched items.
 */
#define N_TEST_GROUPS		10
#define TEST_GROUP_SIZE		10

void testBTreeSeekPrefixKey(void)
{
	// To test handling of internal nodes, the tree has to be tall enough
	// for keys to be stoed in internal node.
	const index32 nTestItems = N_TEST_GROUPS * TEST_GROUP_SIZE;
	const size32 bTreeItemSize = 400;

	// Each group of items shares the first key part. The second part counts from 1, as an
	// item with a zero second part would act as a prefix key when BTreeInsert() compares
	// against it.
	BTree * btree = BTreeCreate(bTreeItemSize, compareTestItemPrefix, 0);
	TestItem * items = Allocate(bTreeItemSize * nTestItems);
	for(index32 group = 0; group < N_TEST_GROUPS; group++) {
		for(index32 i = 0; i < TEST_GROUP_SIZE; i++) {
			initTestItem(
				&items[group * TEST_GROUP_SIZE + i], (group << 16) | (i + 1));
			ASSERT_TRUE(
				BTreeInsert(btree, &items[group * TEST_GROUP_SIZE + i]) == BTREE_INSERTED)
		}
	}
	validateBTree(btree);
	// a group must be able to straddle an internal node, or the test proves nothing
	ASSERT_TRUE(btree->height > 1)

	BTreeIterator iterator;
	BTreeIterate(&iterator, btree);
	for(index32 group = 0; group < N_TEST_GROUPS; group++) {
		TestItem groupKey = {.key = group << 16};
		ASSERT_TRUE(BTreeIteratorSeek(&iterator, &groupKey))

		// the whole group follows, beginning with its first item
		for(index32 i = 0; i < TEST_GROUP_SIZE; i++) {
			index32 itemIndex = group * TEST_GROUP_SIZE + i;
			TestItem const * item = BTreeIteratorPeekItem(&iterator);
			ASSERT_TRUE(testItemsEqual(&items[itemIndex], item))
			ASSERT_TRUE(BTreeIteratorNext(&iterator) == (itemIndex < nTestItems - 1))
		}
	}
	BTreeIteratorEnd(&iterator);

	// BTreePeekItem() answers with the first item of the group as well
	for(index32 group = 0; group < N_TEST_GROUPS; group++) {
		TestItem groupKey = {.key = group << 16};
		TestItem const * item = BTreePeekItem(btree, &groupKey);
		ASSERT_NOT_NULL(item)
		ASSERT_TRUE(testItemsEqual(&items[group * TEST_GROUP_SIZE], item))
	}

	BTreeFree(btree);
	Free(items);
}


int main(int argc, char **argv)
{
	SetupMemory();

	uint32 randomSeed = GenerateRandomSeed();
	// PrintF("seed = %u\n", randomSeed);
 	SetRandomSeed(randomSeed);

	ExecuteTest(testBTreeExample);
    ExecuteTest(testBTreeRandomized);
    ExecuteTest(testBTreeIterator);
	ExecuteTest(testBTreeSeekPrefixKey);

	CleanupMemory();

	TestSummary();
}
