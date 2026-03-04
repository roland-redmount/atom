/**
 * Sorting, reordeing, permuting and searching.
 * 
 */
#ifndef SORT_H
#define SORT_H

#include "platform.h"

/**
 * A comparison function determining the relative order of two items.
 * The function must return -1 if the first items precedes the second;
 * 1 if the first item succeeds the second; and 0 otherwise.
 * If an ItemComparator is used to compare an item against a key,
 * the key is always the second element. This allows for functions that
 * treat keys differently, for example ignoring certain fields.
 * Note that a comparator returning 0 does not necessarily mean that the
 * items are equal (we may have a partial ordering).
 */
typedef int8 (* ItemComparator)(void const * item, void const * itemOrKey, size32 itemSize);

/**
 * Sort a list of items using the specified comparator.
 */
void QuickSort(void * items, size32 nItems, size32 itemSize, ItemComparator compare);

/**
 * Reorder an array of equally sized items.
 */
void ReorderArray(void * array, index8 const * order, size8 nItems, size32 itemSize);

void ReorderRaggedArray(
    void * array, index8 const * order, size32 const * blockSizes, size32 nBlocks);

void FindArrayOrdering(
    void const * data, size8 nItems, size32 itemSize, 
    index8 * ordering, ItemComparator compare);

/**
 * Given an array of items of given size and an item index, return a pointer to
 * the indicated item.
 */
void * ArrayGetItem(void const * items, index32 index, size32 itemSize);

/**
 * Given an array of items and a pointer to an item, return the item's index
 * in the array. It is assumed that the item pointer is aligned to an even
 * item boundary.
 */
index32 ArrayGetItemIndex(void const * items, void const * item, size32 itemSize);

/**
 * Find the first item in the items array such that item >= key according to compare(),
 * using binary search. Returns the index of this item, or nItems if no such item exists,
 * that is, if all items are < key. A pointer to item can be obtained using ArrayGetItem().
 * This function is analogous to C++ std::lower_bound.
 */
index32 BinarySearchLowerBound(
	void const * key, void const * items, size32 nItems, size32 itemSize, ItemComparator compare);

/**
 * Find the first item in the array comparing equal to key.
 * Returns a pointer to the item, or 0 if none exists 
 */
void * BinarySearch(
	void const * key, void const * items, size32 nItems, size32 itemSize, ItemComparator compare);

/**
 * Reorder a byte array accoring to an index vector.
 */
void ReorderByteArray(byte const * array, index8 const * index, size8 n, byte * reordered);

/**
 * Invert a permutation perm. If inv = invertPermutation(perm, n),
 * then y[i] = x[perm[i]] iff y[rev[i]] = x[i].
 */
void InvertPermutation(const index8 * permutation, index8 * inverted, size8 n);

/**
 * Perform a random permutation of the given items array using the
 * Fisher-Yates shuffle method. This yields a sample
 * from a uniform distribution over all possible permutations.
 * 
 * https://en.wikipedia.org/wiki/Random_permutation#Fisher-Yates_shuffles
 */
void RandomPermutation(void * items, size32 nItems, size32 itemSize);


#endif  // SORT_H
