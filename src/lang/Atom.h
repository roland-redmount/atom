#ifndef ATOM_H
#define ATOM_H

#include "platform.h"

/**
 * An atom is a 64-bit value with various interpretations,
 * depending on its type.
 */
typedef union u_Atom {
	// AT_NAME, AT_ID, AT_FORMULA, AT_GENERATOR
	data64 hash;
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
		bool quoted;
	} variable;
	// AT_PARAMETER
	struct {
		uint8 number;
		byte io;
		byte atomType;
	} parameter;
} Atom;

/**
 * Sort an array of atoms according to CompareAtoms()
 */
void SortAtoms(Atom atoms[], size32 nAtoms);

uint8 ReduceAtomsArray(Atom atoms[], uint32 multiplicities[], size8 nAtoms);

int8 CompareAtoms(Atom atom1, Atom atom2);

/**
 * Test two atoms of the same type for identity. An atom is identified by its
 * 64-bit value whatever its type, so this compares the whole atom.
 * Two atoms of different types may share a value; see SameTypedAtoms().
 */
bool SameAtoms(Atom atom1, Atom atom2);

void AcquireAtom(Atom atom, byte atomType);

void ReleaseAtom(Atom atom, byte atomType);


#endif	// ATOM_H
