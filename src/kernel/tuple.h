/**
 * A Tuple consists of an Atom array and atom count.
 */
#ifndef TUPLE_H
#define TUPLE_H

#include "lang/Atom.h"



typedef struct s_Tuple {
	size8 nAtoms;
	Atom atoms[];
} Tuple;

/**
 * Allocate an return a tuple with all atoms set to zero.
 */
Tuple * CreateTuple(size8 nAtoms);

void TupleCopy(Tuple const * source, Tuple * destination);

#endif	// TUPLE_H
