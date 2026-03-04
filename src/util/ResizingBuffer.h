/**
 * A buffer with automatic resizing, useful when creating
 * structures whose size is initially unknown. Contains a
 * single memory block that is reallocated by Reallocate() when needed
 */

#ifndef RESIZINGBUFFER_H
#define RESIZINGBUFFER_H

#include "platform.h"

typedef struct s_ResizingBuffer {
	size32 capacity;	// size of allocated block
	size32 nBytesUsed;	// used size
	byte * block;		// allocated memory block
} ResizingBuffer;

/**
 * Create a buffer with specified capacity
 */
void CreateResizingBuffer(ResizingBuffer * buffer, size32 capacity);

/**
 * Deallocate a buffer
 */
void FreeResizingBuffer(ResizingBuffer * buffer);

/**
 * Get pointer to underlying memory block.
 * The pointer remains valid until the buffer is resized or deallocated.
 */
void * GetBufferMemBlock(ResizingBuffer const * buffer);

/**
 * Find the current number of bytes stored in the buffer
 * (this is different from capacity).
 */
size32 GetBufferSize(ResizingBuffer const * buffer);

/**
 * Resize buffer to a given new size
 */
void SetBufferSize(ResizingBuffer * buffer, size32 newSize);

/**
 * Increase buffer size by a given amount
 */
void IncreaseBufferSize(ResizingBuffer * buffer, size32 nBytes);

/**
 * Write to buffer, expanding if necessary
 */
void AppendToBuffer(ResizingBuffer * buffer, void const * source, size32 size);


#endif	// RESIZINGBUFFER_H
