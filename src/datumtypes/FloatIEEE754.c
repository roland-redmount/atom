

#include "datumtypes/FloatIEEE754.h"


TypedAtom CreateFloat32(float value)
{
	// copy float into low 32 bits of datum
	Datum datum = 0;
	*((float *) &datum) = value;
	return (TypedAtom) {DT_FLOAT32, 0, 0, 0, datum};
}


TypedAtom CreateFloat64(double value)
{
	// copy double, without casting
	Datum datum;
	*((double*) &datum) = value;
	return (TypedAtom) {DT_FLOAT64, 0, 0, 0, datum};;
}


/**
 * Get C floating point values
 */
float GetFloat32Value(TypedAtom float32)
{
	// reinterpret the low 32 bits of the datum as a float
	float f;
	*((data32 *) &f) = (data32) (float32.datum & 0xFFFFFFFF);
	return f;
}

double GetFloat64Value(TypedAtom float64)
{
	// reinterpret the datum as a double
	double d;
	*((data64*) &d) = float64.datum;
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
