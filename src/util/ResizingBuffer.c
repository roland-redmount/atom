
#include "memory/allocator.h"
#include "util/ResizingBuffer.h"


/**
 * Create a resizing buffer of a given initial capacity
 */
void CreateResizingBuffer(ResizingBuffer * buffer, size32 capacity)
{
	buffer->capacity = capacity;
	buffer->nBytesUsed = 0;
	buffer->block = Allocate(capacity);
}


void SetBufferSize(ResizingBuffer * buffer, size32 size)	
{
	if(buffer->capacity < size) {
		// find new capacity
		while(buffer->capacity < size)
			buffer->capacity *= 2;
		// reallocate the memory block
		buffer->block = Reallocate(buffer->block, buffer->capacity);
		ASSERT(buffer->block);
	}
	buffer->nBytesUsed = size;
}


void IncreaseBufferSize(ResizingBuffer * buffer, size32 nBytes)
{
	SetBufferSize(buffer, GetBufferSize(buffer) + nBytes);
}


void AppendToBuffer(ResizingBuffer * buffer, const void * source, size32 nBytes)
{
	// write at end of data
	index32 offset = buffer->nBytesUsed;
	// resize buffer, reallocating if needed
	IncreaseBufferSize(buffer, nBytes);
	// write to current block
	CopyMemory(source, buffer->block + offset, nBytes);
}


size32 GetBufferSize(ResizingBuffer const * buffer)
{
	return buffer->nBytesUsed;
}


void * GetBufferMemBlock(ResizingBuffer const * buffer)
{
	return buffer->block;
}


void FreeResizingBuffer(ResizingBuffer * buffer)
{
	Free(buffer->block);
}
