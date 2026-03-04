/**
 * A polygon is a closed sequence of lines
 */

#ifndef POLYGON_H
#define POLYGON_H

#include "graphics/color.h"
#include "graphics/Point.h"
#include "platform.h"

typedef struct s_Polygon
{
	// list of vertices as flatted array of x, y coordinates
	size32 nVertices;
	float * vertexCoords;
	RGBAColor color;
} Polygon;

// create by copying a list of vertices (can be NULL pointer)
Polygon * CreatePolygon(Point const * vertices, size32 nVertices, RGBAColor color);

Point GetPolygonVertex(Polygon const * polygon, index32 i);
void SetPolygonVertex(Polygon * polygon, index32 i, Point vertex);

void FreePolygon(Polygon const * polygon);


#endif	// POLYGON_H
