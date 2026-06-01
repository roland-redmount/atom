#ifndef ATOM_H
#define ATOM_H

#include "platform.h"

// TODO: this might be better defined as a union {uint64, int64, void *}
// or similar to avoid intepreting casts
typedef data64 Atom;

uint8 ReduceAtomsArray(Atom * atoms, uint32 * multiplicities, size8 nAtoms);

int8 CompareAtoms(Atom atom1, Atom atom2);

#endif	// ATOM_H
