
#include <stdlib.h>

#include "graphics/Polygon.h"


Polygon * CreatePolygon(Point const * vertices, size32 nVertices, RGBAColor color)
{
	Polygon* polygon = malloc(sizeof(Polygon));
	polygon->nVertices = nVertices;
	polygon->color = color;
	polygon->vertexCoords = malloc(2 * nVertices * sizeof(float));
	// copy the given vertices, if any
	if(vertices != NULL) {
		for(index32 i = 0; i < nVertices; i++) {
			polygon->vertexCoords[2*i + 0] = vertices[i].x;
			polygon->vertexCoords[2*i + 1] = vertices[i].y;
		}
	}
	return polygon;
}


Point GetPolygonVertex(Polygon const * polygon, index32 i)
{
	return (Point) {polygon->vertexCoords[2*i], polygon->vertexCoords[2*i + 1]};
}


void SetPolygonVertex(Polygon * polygon, index32 i, Point vertex)
{
	polygon->vertexCoords[2*i] = vertex.x;
	polygon->vertexCoords[2*i + 1] = vertex.y;
}


void FreePolygon(Polygon const * polygon)
{
	free(polygon->vertexCoords);
	free((void*) polygon);
}

