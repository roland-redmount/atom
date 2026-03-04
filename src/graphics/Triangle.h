/**
 * A 2D triangle, defined by three points
 * The order of points determines the winding number
 */

#ifndef TRIANGLE_H
#define TRIANGLE_H

#include "graphics/Point.h"
#include "platform.h"

typedef struct s_Triangle
{
	// this is the same as an array float[6] or float[3][2]
	float x1, y1, x2, y2, x3, y3;
} Triangle;

Triangle * CreateTriangle(void);

Triangle * CopyTriangle(Triangle const * triangle);

void FreeTriangle(Triangle const * triangle);


#endif	// TRIANGLE_H
