
#include "kernel/string.h"
#include "memory/allocator.h"
#include "parser/StringBuffer.h"


#define INITIAL_CAPACITY	10


void StringBufferInit(StringBuffer * buffer)
{
	buffer->buffer = Allocate(INITIAL_CAPACITY);
	buffer->bufferSize = INITIAL_CAPACITY;
	SetMemory(buffer->buffer, buffer->bufferSize, 0);
	buffer->stringLength = 0;
}


void StringBufferPush(StringBuffer * buffer, char c)
{
	if(buffer->stringLength == buffer->bufferSize - 1) {
		buffer->bufferSize = buffer->bufferSize * 2;
		buffer->buffer = Reallocate(buffer->buffer, buffer->bufferSize);
	}
	buffer->buffer[buffer->stringLength++] = c;
}


void StringBufferReset(StringBuffer * buffer)
{
	SetMemory(buffer->buffer, buffer->bufferSize, 0);
	buffer->stringLength = 0;
}


void StringBufferCleanup(StringBuffer * buffer)
{
	Free(buffer->buffer);
}
