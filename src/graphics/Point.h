/**
 * A 2D point
 * This is a light-weight structure, can be passed by value
 */

#ifndef POINT_H
#define POINT_H

typedef struct s_Point {
	float x;
	float y;
} Point;


Point PointAdd(Point point1, Point point2);

Point PointLinearCombine(Point point1, float factor1, Point point2, float factor2);


#endif	// POINT_H

