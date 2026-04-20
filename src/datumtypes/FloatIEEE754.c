

#include "datumtypes/FloatIEEE754.h"


TypedAtom CreateFloat32(float value)
{
	// copy float into low 32 bits of atom
	Atom atom = 0;
	*((float *) &atom) = value;
	return (TypedAtom) {AT_FLOAT32, 0, 0, 0, atom};
}


TypedAtom CreateFloat64(double value)
{
	// copy double, without casting
	Atom atom;
	*((double*) &atom) = value;
	return (TypedAtom) {AT_FLOAT64, 0, 0, 0, atom};;
}


/**
 * Get C floating point values
 */
float GetFloat32Value(TypedAtom float32)
{
	// reinterpret the low 32 bits of the atom as a float
	float f;
	*((data32 *) &f) = (data32) (float32.atom & 0xFFFFFFFF);
	return f;
}

double GetFloat64Value(TypedAtom float64)
{
	// reinterpret the atom as a double
	double d;
	*((data64*) &d) = float64.atom;
	return d;
}


void PrintFloat32(TypedAtom float32)
{
	PrintF("%f", GetFloat32Value(float32));
}		


void PrintFloat64(TypedAtom float64)
{
	PrintF("%f", GetFloat64Value(float64));
}
