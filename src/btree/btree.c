
#include "btree/btree.h"
#include "memory/paging.h"

#define BTREE_NODE_SIZE		MEMORY_PAGE_SIZE

#ifdef DEBUG_ALLOCATE
// When debugging Allocate() we need a different allocator for B-tree memory
#include <stdlib.h>
#define btreeAllocate malloc
#define btreeFree free
#else
#include "memory/allocator.h"
#define btreeAllocate Allocate
#define btreeFree Free
#endif

static BTreeNode * createNode(void)
{
	// Allocated pages are cleared
	return AllocatePage();
}


static void freeNode(BTreeNode * node)
{
	FreePage(node);
}

BTree * BTreeCreate(
	size32 itemSize,
	ItemComparator compareItems,
	void (*freeItem)(void * item, size32 itemSize))
{
	BTree * btree = btreeAllocate(sizeof(BTree));
	SetMemory(btree, sizeof(BTree), 0);

	btree->itemSize = itemSize;
	btree->spareItem = btreeAllocate(itemSize);
	btree->compareItems = compareItems;
	btree->freeItem = freeItem;

	/**
	 * BTREE_NODE_SIZE = sizeof(BTreeNode) + maxItems * itemSize + (maxItems+1) * sizeof(BTreeNode *)
	 * maxItems = 2*degree - 1
	 * minItems = degree - 1
	 * yields the below
	 */
	btree->minDegree = (BTREE_NODE_SIZE - sizeof(BTreeNode) - sizeof(BTreeNode *)) /
        (itemSize + sizeof(BTreeNode *)) / 2;
	btree->nodeMinNItems = btree->minDegree - 1;
	btree->nodeMaxNItems = 2 * btree->minDegree - 1;

	// we start with an empty root node
	btree->root = createNode();
	btree->height = 1;
	
	return btree;
}

/**
 * Some pointer arithmetic on the variable-size item array.
 */
static void * nodeItemArray(BTree const * btree, BTreeNode const * node)
{
	return (void *) (((addr64) node) + sizeof(node) + 2 * btree->minDegree * sizeof(BTreeNode *));
}


static void * nodeGetItem(BTree const * btree,  BTreeNode const * node, index32 index)
{
	return ArrayGetItem(nodeItemArray(btree, node), index, btree->itemSize);
}

static void nodeWriteItem(BTree const * btree,  BTreeNode const * node, index32 index, void const * item)
{
	void * destination = nodeGetItem(btree, node, index);
	CopyMemory(item, destination, btree->itemSize);
}

static void nodeClearItem(BTree const * btree,  BTreeNode const * node, index32 index)
{
	void * item = nodeGetItem(btree, node, index);
	SetMemory(item, btree->itemSize, 0);
}


static bool isLeaf(BTreeNode const * node)
{
	return node->children[0] == 0;
}


static void freeNodeRecursive(BTree * btree, BTreeNode * node)
{
	if(!isLeaf(node)) {
		for(index32 i = 0; i <= node->nItems; i++) 
			freeNodeRecursive(btree, node->children[i]);
	}
	if(btree->freeItem) {
		for(index32 i = 0; i < node->nItems; i++)
			btree->freeItem(nodeGetItem(btree, node, i), btree->itemSize);
	}
	freeNode(node);
}


void BTreeFree(BTree * btree)
{
	freeNodeRecursive(btree, btree->root);
	btreeFree(btree->spareItem);
	btreeFree(btree);
}


void BTreeWriteLock(BTree * btree)
{
	btree->writeLockCount++;
}


void BTreeWriteUnlock(BTree * btree)
{
	ASSERT(btree->writeLockCount > 0)
	btree->writeLockCount--;
}


bool BTreeIsWriteLocked(BTree const * btree)
{
	return btree->writeLockCount >0;
}


bool BTreeLock(BTree * btree)
{
	if(!btree->readLocked) {
		btree->readLocked = true;
		return true;
	}
	else
		return false;
}


void BTreeUnlock(BTree * btree)
{
	ASSERT(btree->readLocked);
	btree->readLocked = false;
}


bool BTreeIsLocked(BTree const * btree)
{
	return btree->readLocked;
}


size32 BTreeHeight(BTree const * btree)
{
	return btree->height;
}


size32 BTreeNItems(BTree const * btree)
{
	return btree->nItemsTotal;
}


/**
 * Search a node for a given key.
 * Returns the index of first item that compares >= to key,
 * or nItems if all items are < key.
 * If the key was not found in the node, this implies that
 * node->children[itemIndex] points to the child node that may contain the key.
*/
static index32 searchNodeItems(BTree const * btree, BTreeNode const * node, void const * key)
{
	void * items = nodeItemArray(btree, node);
	return BinarySearchLowerBound(
		key, items, node->nItems, btree->itemSize, btree->compareItems);
}


void * BTreePeekItem(BTree * btree, void const * keyItem)
{
	if(btree->nItemsTotal == 0)
		return 0;
	BTreeNode * node = btree->root;
	while(true) {
		size32 itemIndex = searchNodeItems(btree, node, keyItem);
		if(itemIndex < node->nItems) {
			void * item = nodeGetItem(btree, node, itemIndex);
			if(btree->compareItems(item, keyItem, btree->itemSize) == 0)
				return item;
		}
		// else no match in this node
		if(isLeaf(node))
			return 0;
		else
			node = node->children[itemIndex];
	}
}


bool BTreeGetItem(BTree * btree, void * key)
{
	void * item = BTreePeekItem(btree, key);
	if(item) {
		CopyMemory(item, key, btree->itemSize);
		return true;
	}
	else
		return false;
}


bool BTreeContainsItem(BTree * btree, const void * key)
{
	return BTreePeekItem(btree, key) != 0;
}

/**
 * Insert an item into a node at the given index.
 * The items at positions index, index+1, ..., nItems (if any)
 * are moved one step to the right.
 * It is assumed that node is not full. If node has zero items,
 * it must still have a valid first child pointer; this case
 * occurs when splitting the root node in BTreeInsert().
 * The given item must precede the one currently at index.
 * Child pointers (if any) are not altered; see insertChildPointer()
 */
static void insertItem(
	BTree const * btree, BTreeNode * node, index32 index, const void * item)
{
	// insert item
	if(node->nItems > index) {
		MoveMemory(
			nodeGetItem(btree, node, index),
			nodeGetItem(btree, node, index + 1),
			(node->nItems - index) * btree->itemSize
		);
	}
	nodeWriteItem(btree, node, index, item);
	node->nItems++;
}

/**
 * Insert a child pointer into a node at the given index.
 * Shifts pointers right of the index 1 step to the right.
 * This does not alter the node's number of items.
 */
static void insertChildPointer(BTree const * btree, BTreeNode * node, index32 index,  BTreeNode * child)
{
	ASSERT(!isLeaf(node))
	if(index < node->nItems) {
		MoveMemory(
			&(node->children[index]),
			&(node->children[index + 1]),
			(node->nItems - index) * sizeof(BTreeNode *)
		);
	}
	node->children[index] = child;
}

/**
 * Split a given child node of a parent.
 * The median item in the child is inserted into the parent node.
 */
static void splitChildNode(BTree const * btree, BTreeNode * parent, index32 childIndex)
{
	// parent node must not be full
	ASSERT(parent->nItems < btree->nodeMaxNItems)
	BTreeNode * leftChild = parent->children[childIndex];
	// child node must be full, so nItems is odd
	ASSERT(leftChild->nItems == btree->nodeMaxNItems)
	// move child items to the right of the median to a new node
	index32 median = leftChild->nItems / 2;
	BTreeNode * rightChild = createNode();
	CopyMemory(
		nodeGetItem(btree, leftChild, median + 1),
		nodeItemArray(btree, rightChild),
		median * btree->itemSize
	);
	CopyMemory(
		&(leftChild->children[median + 1]),
		&(rightChild->children[0]),
		(median + 1) * sizeof(BTreeNode *)
	);
	rightChild->nItems = median;
	leftChild->nItems = median;
	// insert median item into parent node
	void * medianItem = nodeGetItem(btree, leftChild, median);
	insertItem(btree, parent, childIndex, medianItem);
	insertChildPointer(btree, parent, childIndex + 1, rightChild);
	// Clear moved items in leftChild
	// NOTE: this is not strictly necessary,
	// could be skipped for performance
	SetMemory(
		nodeGetItem(btree, leftChild, median + 1),
		median * btree->itemSize,
		0
	);
	SetMemory(
		&(leftChild->children[median + 1]),
		(median + 1) * sizeof(BTreeNode *),
		0
	);
}


BTreeInsertResult BTreeInsert(BTree * btree, void const * item)
{
	// insert a new item into a leaf node, possibly splitting parent nodes
	BTreeNode * node = btree->root;
	if(node->nItems == 0) {
		// insert first item into empty root node
		insertItem(btree, node, 0, item);
		btree->nItemsTotal++;
		return BTREE_INSERTED;
	}
	if(node->nItems == btree->nodeMaxNItems) {
		// Special case for full root node. Create a new root node
		// and make the previous root a child of the new root.
		btree->root = createNode();
		btree->root->children[0] = node;
		btree->height++;
		// split the old root node and retry
		splitChildNode(btree, btree->root, 0);
		return BTreeInsert(btree, item);
	}
	while(true) {
		// At this point we know the current node is not full.
		// Find the pivot point in the current node (where the key might be)
		// The pivot point may be after the last element.
		size32 pivotIndex = searchNodeItems(btree, node, item);
		if(pivotIndex < node->nItems)
		{
			// check for matching key in this node
			void * pivotItem = nodeGetItem(btree, node, pivotIndex);
			if(btree->compareItems(item, pivotItem, btree->itemSize) == 0) {
				// key is already in the tree, so update the current item
				nodeWriteItem(btree, node, pivotIndex, item);
				return BTREE_UPDATED;
			}
		}
		// item is not present in this node
		if(isLeaf(node)) {
			insertItem(btree, node, pivotIndex, item);
			btree->nItemsTotal++;
			return BTREE_INSERTED;
		}
		// else we have an internal node
		BTreeNode * child = node->children[pivotIndex];
		if(child->nItems == btree->nodeMaxNItems) {
			// full child node; split it before proceeding
			splitChildNode(btree, node, pivotIndex);
			// pick the subtree that contains the key
			void * medianItem = nodeGetItem(btree, node, pivotIndex);
			int8 order = btree->compareItems(item, medianItem, btree->itemSize);
			if(order == 0) {
				// Key item was moved into the pivot position
				// when splitting the child node, Update the item.
				nodeWriteItem(btree, node, pivotIndex, item);
				return BTREE_UPDATED;
			}
			else if (order < 0)
				node = child;
			else
				node = node->children[pivotIndex + 1];
		}
		else
			node = child;
	}
	return 0;
}


static void nodeDeleteItem(BTree const * btree, BTreeNode * node, index32 index)
{
	void * item = nodeGetItem(btree, node, index);
	if(btree->freeItem)
		btree->freeItem(item, btree->itemSize);
	// delete item by shifting right side items left
	if(node->nItems > index + 1) {
		MoveMemory(
			nodeGetItem(btree, node, index + 1),
			item,
			(node->nItems - index - 1) * btree->itemSize
		);
	}
	nodeClearItem(btree, node, node->nItems - 1);
	// delete child pointer if needed
	if(!isLeaf(node)) {
		if(node->nItems > index + 1) {
			MoveMemory(
				&(node->children[index + 1]),
				&(node->children[index]),
				(node->nItems - index) * sizeof(BTreeNode *)
			);
		}
		node->children[node->nItems] = 0;
	}
	node->nItems--;
}

/**
 * Merge nodes' child at pivotIndex + 1 into the child at pivotIndex, and
 * move node's item at pivotIndex to the median item of the merged child node.
 * The right child (now empty) is deallocated.
 */
static void mergeChildNodes(BTree * btree, BTreeNode * node, index32 pivotIndex)
{
	BTreeNode * leftChild = node->children[pivotIndex];
	ASSERT(leftChild->nItems < btree->minDegree)
	BTreeNode * rightChild = node->children[pivotIndex + 1];
	ASSERT(rightChild->nItems < btree->minDegree)
	index32 leftChildIndex = leftChild->nItems;
	void * pivotItem = nodeGetItem(btree, node, pivotIndex);
	// move pivot item
	nodeWriteItem(btree, leftChild, leftChildIndex, pivotItem);
	nodeDeleteItem(btree, node, pivotIndex);
	node->children[pivotIndex] = leftChild;
	leftChildIndex++;
	// copy items from right child to left child
	CopyMemory(
		nodeItemArray(btree, rightChild),
		nodeGetItem(btree, leftChild, leftChildIndex),
		(rightChild->nItems) * btree->itemSize
	);
	if(!isLeaf(leftChild)) {
		CopyMemory(
			&(rightChild->children[0]),
			&(leftChild->children[leftChildIndex]),
			(rightChild->nItems + 1) * sizeof(BTreeNode *)
		);
	}
	leftChild->nItems += rightChild->nItems + 1;
	freeNode(rightChild);

	// Merging may result in an empty root node.
	if(node->nItems == 0) {
		ASSERT(node == btree->root)
		// replace empty root with the merged chlld node
		freeNode(btree->root);
		btree->root = leftChild;
		btree->height--;
	}
}

typedef enum {
	BTREE_KEY,
	BTREE_MIN,
	BTREE_MAX
} SearchMode;


static BTreeDeleteResult btreeDeleteRecursive(BTree * btree, BTreeNode * node, SearchMode searchMode, void * item)
{
	while(true) {
		// find the appropriate pivot item, depending on searchMode
		size32 pivotIndex;
		bool match;
		switch(searchMode) {
		case BTREE_KEY:
			pivotIndex = searchNodeItems(btree, node, item);
			if(pivotIndex < node->nItems) {
				void * pivotItem = nodeGetItem(btree, node, pivotIndex);
				match = btree->compareItems(item, pivotItem, btree->itemSize) == 0;
			}
			else
				match = false;
			break;
		case BTREE_MIN:
			pivotIndex = 0;
			match = isLeaf(node);
			break;
		case BTREE_MAX:
			pivotIndex = isLeaf(node) ? node->nItems - 1 : node->nItems;
			match = isLeaf(node);
			break;
		}
		if(match) {
			// copy matching item
			CopyMemory(nodeGetItem(btree, node, pivotIndex), item, btree->itemSize);
			if(isLeaf(node)) {
				nodeDeleteItem(btree, node, pivotIndex);
				btree->nItemsTotal--;
				return BTREE_DELETED;
			}
			else {
				// key found in internal node
				ASSERT(searchMode == BTREE_KEY)
				BTreeNode * leftChild = node->children[pivotIndex];
				if(leftChild->nItems >= btree->minDegree) {
					// recursively delete the last item from the left child subtree,
					// (which is the item immediately preceding the key)
					// and replace the key item with it.
					ASSERT(btreeDeleteRecursive(btree, leftChild, BTREE_MAX, btree->spareItem) == BTREE_DELETED);
					nodeWriteItem(btree, node, pivotIndex, btree->spareItem);
					return BTREE_DELETED;
				}
				BTreeNode * rightChild = node->children[pivotIndex + 1];
				if(rightChild->nItems >= btree->minDegree) {
					// recursively delete the last item from the right child subtree,
					// (which is the item immediately succeding the key)
					// and replace the key item with it.
					ASSERT(btreeDeleteRecursive(btree, rightChild, BTREE_MIN, btree->spareItem) == BTREE_DELETED);
					nodeWriteItem(btree, node, pivotIndex, btree->spareItem);
					// recursively delete the first item from the right child subtree
					return BTREE_DELETED;
				}
				// else neither child has >= minDegree items
				// merge the right child (subtree) into the left
				mergeChildNodes(btree, node, pivotIndex);
				// recursively delete the key item from the merged (left) child
				ASSERT(btreeDeleteRecursive(btree, leftChild, BTREE_KEY, item) == BTREE_DELETED);
				return BTREE_DELETED;
			}
		}
		// key was not found in this node
		if(isLeaf(node)) {
			return BTREE_NO_MATCH;
		}
		// else key is absent from an internal node
		// ensure that the child to be traversed next has >= minDegree items
		BTreeNode * child = node->children[pivotIndex];
		if(child->nItems < btree->minDegree) {
			// find the sibling to the child node
			bool isLeftChild = (pivotIndex < node->nItems);
			uint32 parentItemIndex = isLeftChild ? pivotIndex : node->nItems - 1;
			void * parentItem = nodeGetItem(btree, node, parentItemIndex);
			uint32 siblingIndex = isLeftChild ? pivotIndex + 1 : pivotIndex - 1;
			BTreeNode * sibling = node->children[siblingIndex];
			if(sibling->nItems >= btree->minDegree) {
				// rotate item from sibling to child
				void * siblingItem = nodeGetItem(
					btree, sibling,	isLeftChild ? 0 : sibling->nItems - 1);
				BTreeNode * siblingChild = sibling->children[isLeftChild ? 0 : sibling->nItems];
				insertItem(btree, child, isLeftChild ? child->nItems : 0, parentItem);
				if(!isLeaf(child))
					insertChildPointer(btree, child, isLeftChild ? child->nItems : 0, siblingChild);
				nodeWriteItem(btree, node, parentItemIndex, siblingItem);
				nodeDeleteItem(btree, sibling, isLeftChild ? 0 : sibling->nItems - 1);
			}
			else {
				// Neither child or sibling has >= minDegree items,
				// merge the into the left child node
				mergeChildNodes(btree, node, isLeftChild ? pivotIndex : siblingIndex);
				// make sure we continue with the merged node
				if(!isLeftChild)
					child = sibling;
			}
		}
		node = child;
	}
}


BTreeDeleteResult BTreeDelete(BTree * btree, void * item)
{
	return btreeDeleteRecursive(btree, btree->root, BTREE_KEY, item);
}


void BTreeClear(BTree * btree)
{
	freeNodeRecursive(btree, btree->root);
	// create new root node
	btree->root = createNode();
	btree->nItemsTotal = 0;
	btree->height = 1;
}


/**
 * Advance the iterator by one item. Returns true if a next
 * item was available, in which case iteratorAtEnd()
 * will return false and BTreeIteratorHasItem()
 * will return true when next called.
 */
static bool advanceIterator(BTreeIterator * iterator)
{
	BTreePosition * position = &(iterator->stack[iterator->depth]);
	if(isLeaf(position->node)) {
		// previous item was from a leaf, check for more leaf items
		if(++position->index <= position->node->nItems) {
			// next item in leaf
			return true;
		}
		else {
			// no more items in leaf, back up to ancestor
			while(iterator->depth > 0) {
				position = &(iterator->stack[--iterator->depth]);
				// any items left in this node?
				if(++position->index <= position->node->nItems) {
					// next item is the parent's pivot item
					return true;
				}
			}
			// backed up to root and no more items, end of iteration
			return false;
		}
	}
	else {
		// Previous item was from internal node, or start of iteration,
		// Descend to next leaf
		while(iterator->depth < iterator->btree->height - 1) {
			BTreeNode * child = position->node->children[position->index];
			position = &(iterator->stack[++iterator->depth]);
			position->node = child;
			position->index = 0;
		};
		// leaf index begins at 1 (as if we had returned from a child)
		position->index++;
		return true;
	}
}

static void setIteratorBeforeFirst(BTreeIterator * iterator)
{
	iterator->depth = 0;
	iterator->stack[0].node = iterator->btree->root;
	iterator->stack[0].index = 0;
}


static void setIteratorAtEnd(BTreeIterator * iterator)
{
	iterator->depth = 0;
	iterator->stack[0].node = iterator->btree->root;
	iterator->stack[0].index = iterator->btree->root->nItems + 1;
}


static bool iteratorAtEnd(BTreeIterator const * iterator)
{
	return (iterator->depth == 0) &&
		(iterator->stack[0].index == iterator->btree->root->nItems + 1);
}


/**
 * Position an iterator by searching for an item.
 * Set the iterator position to the first matching item.
 * This is similar to BTreePeekItem() but records the seach path
 * in the iterator stack.
 */ 
static void iteratorSearch(BTreeIterator * iterator, const void * keyItem, ItemComparator compareItemToKey)
{
	setIteratorBeforeFirst(iterator);
	BTreePosition * position = &(iterator->stack[0]);
	if(iterator->btree->nItemsTotal == 0) {
		// iterator has no items, set position to end
		position->index = 1;
		return;
	}
	if(!keyItem) {
		// advance iterator to first position
		advanceIterator(iterator);
		return;
	}
	while(true) {
		position->index = searchNodeItems(iterator->btree, position->node, keyItem);
		if(position->index < position->node->nItems) {
			void const * item = nodeGetItem(iterator->btree, position->node, position->index);
			if(compareItemToKey(item, keyItem, iterator->btree->itemSize) == 0) {
				// iterator index must be after the matching item
				position->index++;
				return;
			}
		}
		// else no match in this node
		if(isLeaf(position->node)) {
			setIteratorAtEnd(iterator);
			return;
		}
		else {
			// advance to next level
			BTreeNode * child = position->node->children[position->index];
			position = &(iterator->stack[++iterator->depth]);
			position->node = child;
			position->index = 0;
		}
	}
}


void BTreeIterate(
	BTreeIterator * iterator, BTree * btree, void const * key, ItemComparator compareItemToKey)
{
	BTreeWriteLock(btree);

	iterator->btree = btree;
	iterator->keyItem = key;
	iterator->stack = btreeAllocate(sizeof(BTreePosition) * btree->height);
	iteratorSearch(iterator, key, compareItemToKey ? compareItemToKey : btree->compareItems);
}


bool BTreeIteratorHasItem(BTreeIterator const * iterator)
{
	return !iteratorAtEnd(iterator);
}


void * BTreeIteratorPeekItem(BTreeIterator const * iterator)
{
	BTreePosition * position = &(iterator->stack[iterator->depth]);
	return nodeGetItem(iterator->btree, position->node, position->index - 1);
}


void BTreeIteratorNext(BTreeIterator * iterator)
{
	advanceIterator(iterator);
}


void BTreeIteratorEnd(BTreeIterator * iterator)
{
	btreeFree(iterator->stack);
	BTreeWriteUnlock(iterator->btree);
	SetMemory(iterator, sizeof(BTreeIterator), 0);
}


/**
 * Recursive tree walking function.
 * Apply the function fn() to each item of each node in the tree in order.
 */
static void itemTraversal(
	BTree const * btree, BTreeNode const * node, ItemCallback callback) 
{
	for(index32 i = 0; i <= node->nItems; i++) {
		if(!isLeaf(node)) {
			// for internal nodes, recurse to next level
            itemTraversal(btree, node->children[i], callback);
		}
		if(i < node->nItems)
			callback(nodeGetItem(btree, node, i));
    }
}


void BTreeTraversal(BTree const * btree, ItemCallback callback) 
{
	itemTraversal(btree, btree->root, callback);
}


#ifdef DEBUG


size32 nodeDeepCount(BTreeNode const * node)
{
    size32 itemCount = node->nItems;
    if(!isLeaf(node)) {
        for(index32 i = 0; i <= node->nItems; i++) {
            itemCount += nodeDeepCount(node->children[i]);
        }
    }
    return itemCount;
}


size32 BTreeDeepCount(BTree const * btree)
{
    return nodeDeepCount(btree->root);
}

/**
 * Test that the B-tree under the given node is balanced
 * by checking that all leaves have the given depth.
 */
static bool nodeIsBalanced(BTreeNode const * node, int depth)
{
    if(isLeaf(node))
        return depth == 1;
    else {
        for(index32 i = 0; i <= node->nItems; i++) {
            if(!nodeIsBalanced(node->children[i], depth - 1))
                return false;
		}
	    return true;
    }
}


bool BTreeIsBalanced(BTree const * btree)
{
    return nodeIsBalanced(btree->root, btree->height);
}


static bool nodeVerifyBounds(BTree const * btree, BTreeNode const * node, int depth)
{
	// check node size is within bounds
	if(node->nItems > btree->nodeMaxNItems)
		return false;
	if(depth > 1 && node->nItems < btree->nodeMinNItems)
		return false;
	// validate children
    if(!isLeaf(node)) {
        for(index32 i = 0; i <= node->nItems; i++) {
            if(!nodeVerifyBounds(btree, node->children[i], depth + 1))
                return false;
        }
    }
    return true;
}


bool BTreeVerifyBounds(BTree const * btree)
{
	if(btree->nItemsTotal > 0 && btree->root->nItems == 0)
		return false;
    return nodeVerifyBounds(btree, btree->root, 1);
}

/**
 * Recursively validate the order of items in the sub-tree
 * under the given node. If *bound is not 0, it must point to
 * an item that should compare < to all items in the sub-tree.
 * Updates *bound to point to the last item in the sub-tree.
 */
static bool nodeVerifyItemOrder(BTree const * btree, BTreeNode const * node, void const ** bound)
{
	void const * items = nodeItemArray(btree, node);
	for(index32 i = 0; i <= node->nItems; i++) {
		if(!isLeaf(node)) {
			if(!nodeVerifyItemOrder(btree, node->children[i], bound))
				return false;
		}
		if(i < node->nItems) {
			void const * next = ArrayGetItem(items, i, btree->itemSize);
			if(*bound && (btree->compareItems(*bound, next, btree->itemSize) >= 0))
				return false;
			*bound = next;
		}
	}
	return true;
}


bool BTreeVerifyItemOrder(BTree const * btree)
{
	void const * bound = 0;
	return nodeVerifyItemOrder(btree, btree->root, &bound);
}


static void printIndentation(size32 indent)
{
	for(index32 i = 0; i < indent; i++)
		PrintChar(' ');

}

static void nodePrint(
	BTree const * btree, BTreeNode const * node, void (*printItem)(void const * item), size32 indent) 
{
	if(isLeaf(node)) {
		// print leaves on a single line
		printIndentation(indent);
		for(index32 i = 0; i < node->nItems; i++) {
			printItem(nodeGetItem(btree, node, i));
			PrintChar(' ');
		}
		PrintChar('\n');
	}
	else {
		for(index32 i = 0; i <= node->nItems; i++) {
			// internal node, recurse to next level with increased indent
			nodePrint(btree, node->children[i], printItem, indent + 2);
			// print nodes on seperate lines
			if(i < node->nItems) {
				printIndentation(indent);
				printItem(nodeGetItem(btree, node, i));
				PrintChar('\n');
			}
		}
    }
}


void BTreePrint(BTree const * btree, void (*printItem)(void const * item))
{
	PrintCString("root\n");
	nodePrint(btree, btree->root, printItem, 0);
}


#endif
