
#include "util/ResizingArray.h"


void CreateResizingArray(ResizingArray * array, size32 elementSize, size32 capacity)
{
	array->elementSize = elementSize;
	array->nElements = 0;
	size32 capacityBytes = capacity * elementSize;
	CreateResizingBuffer(&(array->buffer), capacityBytes);
}


void FreeResizingArray(ResizingArray * array)
{
	FreeResizingBuffer(&array->buffer);
}


void * ResizingArrayGetMemory(ResizingArray const * array)
{
	return GetBufferMemBlock(&(array->buffer));
}


void * ResizingArrayGetElement(ResizingArray const * array, index32 index)
{
	byte * bufferBytes = GetBufferMemBlock(&(array->buffer));
	return bufferBytes + (index * array->elementSize);
}


size32 ResizingArrayNElements(const ResizingArray * array)
{
	return array->nElements;
}


void ResizingArrayAppend(ResizingArray * array, const void * element)
{
	AppendToBuffer(&(array->buffer), element, array->elementSize);
	array->nElements++;
}


void ResizingArrayReset(ResizingArray * array)
{
	SetBufferSize(&(array->buffer), 0);
	array->nElements = 0;
}
