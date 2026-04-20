

#include "kernel/FloatIEEE754.h"


Atom CreateFloat32(float value)
{
	// copy float into low 32 bits of atom
	Atom atom = 0;
	CopyMemory(&value, &atom, 4);
	return atom;
}


Atom CreateFloat64(double value)
{
	// copy double, without casting
	Atom atom;
	CopyMemory(&value, &atom, 8);
	return atom;
}


/**
 * Get C floating point values
 */
float GetFloat32Value(Atom float32)
{
	// reinterpret the low 32 bits of the atom as a float
	float f;
	CopyMemory(&float32, &f, 4);
	return f;
}

double GetFloat64Value(Atom float64)
{
	// reinterpret the atom as a double
	double d;
	CopyMemory(&float64, &d, 8);
	return d;
}


void PrintFloat32(Atom float32)
{
	PrintF("%f", GetFloat32Value(float32));
}		


void PrintFloat64(Atom float64)
{
	PrintF("%f", GetFloat64Value(float64));
}
