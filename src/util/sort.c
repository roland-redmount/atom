
#include <stdlib.h>
#include <time.h>

#include "util/sort.h"

/**
 * NOTE: quicksort algorithm adapted from
 * https://github.com/portfoliocourses/c-example-code/blob/main/quicksort.c
 *
 * To avoid excessive memory copying, we do not actually sort the array in-place,
 * but instead sort a vector of byte indices into the array.
 * The sorted array can then be obtains in one round of copying.
 * NOTE: this was probably a bad idea, the reordering is also tricky ...
 */ 

static inline void swapIndices(index32 * indexArray, index32 i, index32 j)
{
	index32 temp = indexArray[i];
	indexArray[i] = indexArray[j];
	indexArray[j] = temp;
}

// partition the index array between low - high indexes by a pivot value
// return the index of the pivot
static index32 partition(
    void const * items, index32 * indexArray, index32 low, index32 high, size32 itemSize,
    ItemComparator compare)
{
	// randomly select a pivot element from the range
	index32 pivot = low + (rand() % (high - low));

	// swap the element at the pivot_index with last element
	if(pivot != high)
		swapIndices(indexArray, pivot, high);

	// the pivot item is now at the high index
    void const * pivotItem = ((byte *) items) + indexArray[high]*itemSize;

    // the partioning algorithm will shift elements that are less than the pivot
    // value to the front portion of the low - high array indexes, with i keeping
    // track of where the elements that are greater than the pivot value begin
    index32 i = low;

	for(index32 j = low; j < high; j++) {
        byte const * item_j = ((byte *) items) + indexArray[j]*itemSize;
		if(compare(item_j, pivotItem, itemSize) <= 0)	{
            // item j should precede the pivot item
			swapIndices(indexArray, i, j);
			i++;
		}
	}
	// now the item at index i is greater than or equal to the pivot item
	// so we swap it with the pivot item to complete the partition
	swapIndices(indexArray, i, high);

	// index i now contains the pivot value
	return i;
}

// applies the recursive divide and conquer portion of the quicksort algorithm
// to the array... applying quicksort to the array between the low-high indexes
static void quicksort_recursive(
    void const * items, index32 * indexArray, index32 low, index32 high, size32 itemSize,
    ItemComparator compare)
{
	// partition the array by a pivot item
	int pivotIndex = partition(items, indexArray, low, high, itemSize, compare);

    // depth first recursion
    if(low + 1 < pivotIndex)
		quicksort_recursive(items, indexArray, low, pivotIndex - 1, itemSize, compare);   // left subarray
	if(pivotIndex + 1 < high)
        quicksort_recursive(items, indexArray, pivotIndex + 1, high, itemSize, compare);  // right subarray
}


void QuickSort(void * items, size32 nItems, size32 itemSize, ItemComparator compare)
{
    if(nItems < 2) {
        return;     // nothing to do
    }

	// seed the random number generator
	srand(time(NULL));

    // fall back to memcmp() as comparator if compare() is not provided
    ItemComparator _compare = compare ? compare : CompareMemory;

    index32 indexArray[nItems];
    for(index32 i = 0; i < nItems; i++) {
        indexArray[i] = i;
    }
    // sort the index array
	quicksort_recursive(items, indexArray, 0, nItems - 1, itemSize, _compare);

    // reorder the array
    // NOTE: needs temporary copy on stack
    byte tmp[nItems*itemSize];
    for(index8 i = 0; i < nItems; i++) {
        // printf(" %u", indexArray[i]);
        CopyMemory(((byte *) items) + indexArray[i]*itemSize, tmp + i*itemSize, itemSize);
    }
    // printf("\n");
    // copy back to array
    CopyMemory(tmp, items, nItems*itemSize);
}


/**
 * Permute a "ragged" byte array containing n blocks of different sizes.
 * Here order is a vector such that order[i] is the block to place at position i .
 */

void ReorderRaggedArray(
    void * array, index8 const * order, size32 const * blockSizes, size32 nBlocks)
{
    // compute block offsets as cumulative sums; last element is the size of the array
    index32 blockOffsets[nBlocks + 1];
    blockOffsets[0] = 0;
    for(index8 i = 0; i < nBlocks; i++)
        blockOffsets[i+1] = blockOffsets[i] + blockSizes[i];

    // temporary copy
    byte copy[blockOffsets[nBlocks]];
    index32 copyOffset = 0;
    for(index8 i = 0; i < nBlocks; i++) {
        CopyMemory(((byte *) array) + blockOffsets[order[i]], copy + copyOffset, blockSizes[order[i]]);
        copyOffset +=  blockSizes[order[i]];
    }
    // copy back to array
    CopyMemory(copy, array, blockOffsets[nBlocks]);
}


/**
 * Reorder (sort) an array of items, all of the same itemSize (in bytes), according to the order array.
 */
void ReorderArray(void * array, index8 const * order, size8 nItems, size32 itemSize)
{
    // temporary c on stack
    byte tmp[nItems*itemSize];
    for(index8 i = 0; i < nItems; i++) {
        CopyMemory(((byte *) array) + order[i]*itemSize, tmp + i*itemSize, itemSize);
    }
    // copy back to array
    CopyMemory(tmp, array, nItems*itemSize);
}


/**
 * Generic ordering function for an array of up to 255 items of the given size (bytes)
 * The compare function is used to determine ordering between any two items
 * Writes to the given ordering array so that its i'th element is index of the item
 * in sorted position i
 * If compare function is NULL, we fall back on memcmp()
 * 
 * TODO: this could be replaced with QuickSort (above) except this uses 8-byte ordering array
 */ 

void FindArrayOrdering(
    void const * data, size8 nItems, size32 itemSize, 
    index8 * ordering, ItemComparator compare)
{
    byte const * bytes = (byte const *) data;
    // initalize ordering to 1 ... n
    for(index8 i = 0; i < nItems; i++) 
        ordering[i] = i;
    // do a simple bubble sort for now ...  :-/
    bool sorted = false;
    while(!sorted) {
        sorted = true;
        for(index8 i = 1; i < nItems; i++) {
            // compare two words now adjacent in the *reordered* array
            byte const * item1 = bytes + ordering[i-1] * itemSize;
            byte const * item2 = bytes + ordering[i] * itemSize;
            int8 sign = compare ? compare(item1, item2, itemSize) : CompareMemory(item1, item2, itemSize);
            if(sign > 0) {
                // word2 should be placed before word1
                // swap orders
                index8 tmp = ordering[i-1];
                ordering[i-1] = ordering[i];
                ordering[i] = tmp;
                // set flag
                sorted = false;
            }
        }
    }
}


// The below is based on
// https://stackoverflow.com/questions/39416560/how-can-i-simplify-this-working-binary-search-code-in-c

index32 BinarySearchLowerBound(
	void const * key, void const * items, size32 nItems, size32 itemSize, ItemComparator compare)
{
    index32 lower = 0;
    index32 upper = nItems;
    while(lower < upper)
    {
    	// test halfway between lower and upper bound
        index32 pos = lower + (upper - lower)/2;
        int sign = compare(((byte *) items) + pos*itemSize, key, itemSize);
        if(sign == -1)
            lower = pos + 1;	// pos < key, increase lower bound
   		else
            upper = pos;	// pos >= key, reduce upper bound
    }
    // at this point, we always have lower == upper
    // and upper is the first item that is >= key,
	// upper = nItems if no such item exists
	return upper;
}


void ReorderByteArray(byte const * array, index8 const * index, size8 n, byte * reordered)
{
    for(index8 i = 0; i < n; i++)
        reordered[i] = array[index[i]];
}


void InvertPermutation(const index8 * permutation, index8 * inverted, size8 n)
{
    // compute reverse permutation
    for(index8 i = 0; i < n; i++) {
        inverted[permutation[i]] = i;
    }
}


void * ArrayGetItem(void const * items, index32 index, size32 itemSize)
{
	return (void *) (((addr64) items) + index * itemSize);
}


index32 ArrayGetItemIndex(void const * items, void const * item, size32 itemSize)
{
	return (((addr64) item) - ((addr64) items)) / itemSize;
}


void RandomPermutation(void * items, size32 nItems, size32 itemSize)
{
    if(nItems < 2) 
		return;
    byte tmp[itemSize];
    byte * itemsBytes = items;
    for(index32 i = 0; i < nItems - 1; i++) {
        // swap element i for a random element j such that i <= j < n
        index32 j = RandomInteger(i, nItems-1);
		CopyMemory(itemsBytes + j * itemSize, tmp, itemSize);
        CopyMemory(itemsBytes + i * itemSize, itemsBytes + j * itemSize, itemSize);
        CopyMemory(tmp, itemsBytes + i * itemSize, itemSize);
    }
}
