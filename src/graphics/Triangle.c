
#include <stdlib.h>

#include "graphics/Triangle.h"


Triangle * CreateTriangle()
{
	return malloc(sizeof(Triangle));
}


Triangle * CopyTriangle(Triangle const * triangle)
{
	// TODO
	ASSERT(false);
	return 0;
}


void FreeTriangle(Triangle const * triangle)
{
	free((void*) triangle);
}

