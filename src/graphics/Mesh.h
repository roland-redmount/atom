/**
 * A mesh of triangles, graphics primitive 
 */

#ifndef MESH_H
#define MESH_H

#include "graphics/color.h"
#include "graphics/Triangle.h"


typedef struct s_Mesh
{
	// store as a list of 3*nTriangles vertex coordinates
	float * coords;
	size32 nTriangles;
	RGBAColor color;
} Mesh;

Mesh * CreateMesh(Triangle const * triangles, size32 nTriangles, RGBAColor color);

// create a mesh for rectangle ("quad")
Mesh * CreateRectangleMesh(Point bottomLeft, Point topRight, RGBAColor color);

void FreeMesh(Mesh const * mesh);

// TODO: methods for triangulating a polygon to a mesh


#endif	// MESH_H

