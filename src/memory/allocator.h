#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "platform.h"


void CreateAllocator(void * memoryArea, size8 log2AreaSize);
void CloseAllocator(void);

/**
 * Allocate a memory block of at least the given size (in bytes).
 * The actual size can be found using GetAllocatedSize().
 * The returned memory is cleared.
 */

#ifdef DEBUG_ALLOCATE
void * _LogAllocate(char const * fileName, uint32 lineNumber, size32 allocSize);
#define Allocate(allocSize) _LogAllocate(__FILE__, __LINE__, allocSize)
#else
void * Allocate(size32 size);
#endif

/**
 * Reallocate the given memory block fit the the new size, if necessary.
 * Returns a pointer to the new memory block.
 * If the memory block was reallocated, it is moved and the previous
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
 *
 * CLAUDE: the memory is const because freeing it is not a modification the caller
 * can observe: the block's lifetime ends here. This lets a caller holding a
 * const pointer release it without a cast; see "Use of const" in code-conventions.md.
 */
#ifdef DEBUG_ALLOCATE
void _LogFree(char const * fileName, uint32 lineNumber, void const * block);
#define Free(memory) _LogFree(__FILE__, __LINE__, memory)
#else
void Free(void const * memory);
#endif

/**
 * Return the size (in bytes) of an allocated memory block.
 * TOOD: This is includes the block header, we should probably
 * report the usable size.
 */
size32 GetAllocatedSize(void const * memory);

/**
 * Number of bytes currently available for allocation.
 */
size32 AllocatorNBytesFree(void);

/**
 * Number of bytes currently allocated.
 */
size32 AllocatorNBytesAllocated(void);

/**
 * Maxmimal number of bytes available in this allocator (when empty).
 */
size32 AllocatorMaxNBytes(void);


// for debugging
void PrintFreeLists(void);
void DumpAllocatedBlocks(void);
bool AllocatorIsEmpty(void);

#ifdef DEBUG_ALLOCATE
/**
 * Call with status = true to activate logging, false to inactivate.
 */
void SetAllocationLogging(bool status);

void DumpAllocateLog(void);
#endif

#endif // ALLOCATOR_H
