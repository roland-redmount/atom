

#include "datumtypes/FloatIEEE754.h"


Atom CreateFloat32(float value)
{
	// copy float into low 32 bits of datum
	Datum datum = 0;
	*((float *) &datum) = value;
	return (Atom) {DT_FLOAT32, 0, 0, 0, datum};
}


Atom CreateFloat64(double value)
{
	// copy double, without casting
	Datum datum;
	*((double*) &datum) = value;
	return (Atom) {DT_FLOAT64, 0, 0, 0, datum};;
}


/**
 * Get C floating point values
 */
float GetFloat32Value(Atom float32)
{
	// reinterpret the low 32 bits of the datum as a float
	float f;
	*((data32 *) &f) = (data32) (float32.datum & 0xFFFFFFFF);
	return f;
}

double GetFloat64Value(Atom float64)
{
	// reinterpret the datum as a double
	double d;
	*((data64*) &d) = float64.datum;
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
