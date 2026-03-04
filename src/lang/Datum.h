#ifndef DATUM_H
#define DATUM_H

#include "platform.h"

typedef data64 Datum;

// pointer to a function releasing a datum
typedef void (*ReleaseFunction)(Datum);

uint8 ReduceDatumArray(Datum * datums, uint32 * multiplicities, size8 nDatums);

int8 CompareDatums(Datum datum1, Datum datum2);

#endif	// DATUM_H
