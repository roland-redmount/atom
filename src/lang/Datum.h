#ifndef DATUM_H
#define DATUM_H

#include "platform.h"

typedef data64 Datum;

uint8 ReduceDatumArray(Datum * datums, uint32 * multiplicities, size8 nDatums);

int8 CompareDatums(Datum datum1, Datum datum2);

#endif	// DATUM_H
