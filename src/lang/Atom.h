#ifndef ATOM_H
#define ATOM_H

#include "platform.h"

/**
 * An atom is a 64-bit value with various interpretations,
 * depending on its type.
 */
typedef union u_Atom {
	// AT_NAME, AT_ID
	data64 hash;
	// AT_UNT
	uint64 _uint;
	// AT_INT
	int64 _int;
	// AT_FLOAT
	float64 _float;
	// AT_LETTER
	struct {
		uint8 code;
	} letter;
	// AT_VARIABLE
	struct {
		char name;
		byte type;
		uint8 quoteCount;
	} variable;
	// AT_PARAMETER
	struct {
		uint8 number;
		byte io;
		byte atomType;
	} parameter;
} Atom;

uint8 ReduceAtomsArray(Atom * atoms, uint32 * multiplicities, size8 nAtoms);

int8 CompareAtoms(Atom atom1, Atom atom2);

#endif	// ATOM_H
