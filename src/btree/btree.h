/**
 * An in-memory B-tree implementation.
 */

 #ifndef BTREE_H
 #define BTREE_H

#include "util/sort.h"


typedef struct s_BTreeNode BTreeNode; 

struct s_BTreeNode {
	size32 nItems;
	BTreeNode * children[];
};


 typedef struct s_BTree {
	BTreeNode * root;
	size32 nItemsTotal;
	size32 height;
	size32 minDegree;		// minimum no. children in a node (except the root)
	size32 itemSize;

	size32 nodeMinNItems;	// these are redundant, but convenient
	size32 nodeMaxNItems;

	ItemComparator compareItems;
	void (*freeItem)(void * item, size32 itemSize);
	
	uint32 writeLockCount;	// semaphore preventing mutating operations
	bool readLocked;		// lock for exclusive access

	// Temporary item for returning copies from delete operations.
	// NOTE: this is not thread-safe, but mutating operations
	// cannot occur concurrently anyway.
	void * spareItem;		

 } BTree;


/**
 * Create a new B-tree.
 * 
 * The compareItems() function is used to order items in the tree,
 * and also to compare items to query keys, unless another function
 * is provided. For queries, the second item is the key.
 */

BTree * BTreeCreate(
	size32 item_size,
	ItemComparator compareItems,
	void (*freeItem)(void * item, size32 itemSize)
);


void BTreeFree(BTree * btree);


/**
 * Write-lock the B-tree, preventing any modification until BTreeWriteUnlock() is called.
 * Any attempt to modify the b-tree while write-locked will trigger ASSERT.
 * BTreeWriteLock() may be called multiple times; if so, BTreeWriteUnlock() must be
 * called the same number of times before the tree is unlocked. 
 *
 * NOTE: it might be possible to lock only individual nodes,
 * or even items, for writing?
 */
void BTreeWriteLock(BTree * btree);
void BTreeWriteUnlock(BTree * btree);
bool BTreeIsWriteLocked(BTree const * btree);


/**
 * Lock the b-tree for all operations. Fails if the b-tree is already
 * locked or write-locked.
 */
bool BTreeLock(BTree * btree);
void BTreeUnlock(BTree * btree);
bool BTreeIsLocked(BTree const * btree);

size32 BTreeHeight(BTree const * btree);
size32 BTreeNItems(BTree const * btree);


/**
 * Return a pointer to an item stored in the B-tree, matching
 * the given key. The pointer is only valid as long as the B-tree
 * is not modified. The caller must not modify the item in a way
 * that alters its ordering vs. other items in the tree.
 * If the item is not found, returns 0.
 */
void * BTreePeekItem(BTree * btree, void const * key);

/**
 * Similar to BTreePeekItem(), but copies the item to *key
 * if it is found.
 * Returns true if the item was found. If the item is not
 * found, key is not altered.
 */
bool BTreeGetItem(BTree * btree, void * key);

bool BTreeContainsItem(BTree * btree, const void * key);


typedef enum e_BTreeInsertResult {
	BTREE_INSERTED = 1,
	BTREE_UPDATED,
} BTreeInsertResult;

/**
 * Insert an item into the B-tree, or update an existing item
 * if one exists that compares equal by compareItemToKey()
 */
BTreeInsertResult BTreeInsert(BTree * btree, void const * item);


typedef enum e_BTreeDeleteResult {
	BTREE_DELETED = 1,
	BTREE_NO_MATCH,
} BTreeDeleteResult;

/**
 * Delete an item that compares equal to the given item
 * by compareItems(). The deleted item is written to *item.
 */
BTreeDeleteResult BTreeDelete(BTree * btree, void * item);

/**
 * Delete all items in the B-tree.
 */
void BTreeClear(BTree * btree);

/**
 * Iterate over items in a B-tree, starting from the first item
 * that compare equal to the given key according to the 
 * function compareItemToKey(item. key). compareItemToKey() must be compatible
 * with the B-tree's compareItems() function in the sense that
 * item1 > key and item2 > item2 implies item2 > key. This ensures
 * that we can search the B-tree using compareItemToKey() without
 * missing any items that compare equal to the key.
 * 
 * If compareItemToKey is 0, the B-tree's compareItems() function
 * is used. If key is 0, the iteration is over all items in the tree. 
 *
 * Locks the B-tree for writing.
 */

typedef struct s_BTreePosition {
	BTreeNode const * node;
	index32 index;
} BTreePosition;

typedef struct s_BTreeIterator {
	BTree * btree;
	void const * keyItem;
	BTreePosition * stack;
	size32 depth;	// between 0 and btree->height - 1
} BTreeIterator;


void BTreeIterate(
	BTreeIterator * iterator, BTree * btree, void const * keyItem,
	ItemComparator compareItemToKey);

bool BTreeIteratorHasItem(BTreeIterator const * iterator);

void const * BTreeIteratorPeekItem(BTreeIterator const * iterator);

void BTreeIteratorNext(BTreeIterator * iterator);

void BTreeIteratorEnd(BTreeIterator * iterator);


/**
 * Traverse the B-tree breadth-first and apply a callback function to each item
 */

typedef void (*ItemCallback)(void const * item);

void BTreeTraversal(BTree const * btree, ItemCallback callback);


/**************************************************************************
 * 
 * Debug functions.
 * These are only used for testing purposes; see test_btree.c
 * 
 **************************************************************************/

#ifdef DEBUG

/**
 * Recursively count the number of items in the btree.
 */
size32 BTreeDeepCount(BTree const * btree);

/**
 * Verify that the B-tree is balanced, meaning that all
 * leaf nodes are at the same depth
 */
bool BTreeIsBalanced(BTree const * btree);

/**
 * Verify that all nodes in the B-tree satisfy the minimum
 * and maximum item bounds.
 */
bool BTreeVerifyBounds(BTree const * btree);


/**
 * Verify that all items in the tree is in the correct order.
 */
bool BTreeVerifyItemOrder(BTree const * btree);

/**
 * Simple function for printing a BTree.
 * The printItem() function is assumed to print a short item representation without newline.
 * Don't try this on very large trees ... 
 */
void BTreePrint(BTree const * btree, void (*printItem)(void const * item));

#endif


#endif	// BTREE_H
