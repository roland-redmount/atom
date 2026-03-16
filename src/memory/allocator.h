#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "platform.h"


void CreateAllocator(void * memoryArea, size8 log2AreaSize);
void CloseAllocator(void);

/**
 * Allocate a memory block of at least the given size (in bytes).
 * The actual size can be found using GetAllocatedSize().
 * The returned memory is not cleared.
 */
void * Allocate(size32 size);

/**
 * Reallocate the given memory block fit the the new size, if necessary.
 * Returns a pointer to the new memory block.
 * If the memory block was reallocated, it is moved and the the previous
 * pointer is invalid; if not, the memory pointer is returned.
 * If the given memory pointer is null, this is equivalent to Allocate().
 * 
 * Reallocate() is cheap when reallocation is not needed, so the pattern
 * 
 * memory = Reallocate(memory, newSize)
 * 
 * can be used to ensure the memory block is of the required size. 
 */
void * Reallocate(void * memory, size32 newSize);

/**
 * Free a previously allocated memory block.
 */
void Free(void * memory);

/**
 * Return the size (in bytes) of an allocated memory block.
 * TOOD: This is includes the block header, we should probably
 * report the usable size.
 */
size32 GetAllocatedSize(void * memory);

/**
 * Return the number of bytes available for allocation
 * in the allocator
 */
size32 GetTotalFree(void);


// for debugging
void PrintFreeLists(void);
void DumpAllocatedBlocks(void);
bool AllocatorIsEmpty(void);


#endif // ALLOCATOR_H
