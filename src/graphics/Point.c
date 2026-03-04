
#include "graphics/Point.h"


Point PointAdd(Point point1, Point point2)
{
	Point sum;
	sum.x = point1.x + point2.x;
	sum.y = point1.y + point2.y;
	return sum;
}


Point PointLinearCombine(Point point1, float factor1, Point point2, float factor2)
{
	Point combination;
	combination.x = point1.x * factor1 + point2.x * factor2;
	combination.y = point1.y * factor1 + point2.y * factor2;
	return combination;
}
