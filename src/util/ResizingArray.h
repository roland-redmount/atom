/**
 * A convenience wrapper around ResizingBuffer to represent
 * an array of elements of a fixed size.
 */

#ifndef RESIZINGARRAY_H
#define RESIZINGARRAY_H

#include "util/ResizingBuffer.h"


typedef struct s_ResizingArray {
	size32 elementSize;
	size32 nElements;
	ResizingBuffer buffer;
} ResizingArray;

/**
 * Create array with given element size and initial capacity (in number of elements)
 */
void CreateResizingArray(ResizingArray * array, size32 elementSize, size32 capacity);

/**
 * Deallocate an array
 */
void FreeResizingArray(ResizingArray * array);

/**
 * Get pointer to underlying memory block.
 * The pointer remains valid until the array is resized or deallocated.
 */
void * ResizingArrayGetMemory(ResizingArray const * array);


/**
 * Get pointer to a specific element in the array.
 * Index must be < array size.
 */
void * ResizingArrayGetElement(ResizingArray const * array, index32 index);


/**
 * Current number of elements stored in the array.
 */
size32 ResizingArrayNElements(const ResizingArray * array);


/**
 * Append an element to array
 */
void ResizingArrayAppend(ResizingArray * array, const void * element);

/**
 * Rset an array to zero size
 */
void ResizingArrayReset(ResizingArray * array);


#endif	// RESIZINGARRAY_H
