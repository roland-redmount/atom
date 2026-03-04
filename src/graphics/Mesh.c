
#include <stdlib.h>

#include "graphics/Mesh.h"

/**
 * Create mesh by copying an array of triangles
 */
Mesh * CreateMesh(Triangle const * triangles, size32 nTriangles, RGBAColor color)
{
	Mesh* mesh = malloc(sizeof(Mesh));
	mesh->nTriangles = nTriangles;
	mesh->color = color;

	// copy triangles, 6 coords each
	mesh->coords = malloc(nTriangles * 6 * sizeof(float));
	for(index32 i = 0; i < nTriangles; i++) {
		mesh->coords[6*i + 0] = triangles[i].x1;
		mesh->coords[6*i + 1] = triangles[i].y1;
		mesh->coords[6*i + 2] = triangles[i].x2;
		mesh->coords[6*i + 3] = triangles[i].y2;
		mesh->coords[6*i + 4] = triangles[i].x3;
		mesh->coords[6*i + 5] = triangles[i].y3;
	}
	return mesh;
}


/**
 * Create a mesh for rectangle ("quad")
 * 
 * TODO: what if bottomLeft.x > topRight.x or bottomLeft.y > topRight.y ?
 */
Mesh * CreateRectangleMesh(Point bottomLeft, Point topRight, RGBAColor color)
{
	Mesh* mesh = malloc(sizeof(Mesh));
	mesh->nTriangles = 2;
	mesh->color = color;

	mesh->coords = malloc(2 * 6 * sizeof(float));
	// lower left triangle
	mesh->coords[0] = bottomLeft.x; 
	mesh->coords[1] = bottomLeft.y;
	mesh->coords[2] = bottomLeft.x;
	mesh->coords[3] = topRight.y;
	mesh->coords[4] = topRight.x;
	mesh->coords[5] = bottomLeft.y;
	// upper right triangle
	mesh->coords[6] = bottomLeft.x;
	mesh->coords[7] = topRight.y;
	mesh->coords[8] = topRight.x;
	mesh->coords[9] = topRight.y;
	mesh->coords[10] = topRight.x;
	mesh->coords[11] = bottomLeft.y;

	return mesh;
}


void FreeMesh(const Mesh* mesh)
{
	free(mesh->coords);
	free((void*) mesh);	
}

