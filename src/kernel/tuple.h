/**
 * A Tuple consists of an Atom array and atom count.
 * NOTE: this might not be necesssary; can use Atom[] atoms, size8 nAtoms
 */
#ifndef TUPLE_H
#define TUPLE_H

#include "lang/Atom.h"


void TupleCopy(Atom const sourceTuple[], Atom destinationTuple[], size8 nAtoms);

int8 TupleCompare(Atom const tuple1[], Atom const tuple2[], size8 nAtoms);

void PrintTuple(byte const atomTypes[], Atom const tuple[], size8 nAtoms);

#endif	// TUPLE_H
