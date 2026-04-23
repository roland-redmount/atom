/**
 * An typed atom stores an atom type and an atom.
 * See DatumType.h
 */

#ifndef TYPEDATOM_H
#define TYPEDATOM_H

#include "lang/Atom.h"
#include "lang/AtomType.h"

typedef struct s_TypedAtom
{
	byte type;
	byte reserved[3];
	Atom atom;
} __attribute__((packed)) TypedAtom;


// the invalid atom, only used internally to signal errors
extern TypedAtom invalidAtom;

/**
 * Shorthand for (TypedAtom) {.type = type, .atom = atom}
 */
TypedAtom CreateTypedAtom(byte type, Atom atom);

// reference handling
void AcquireTypedAtom(TypedAtom atom);
void ReleaseTypedAtom(TypedAtom atom);

// compare two typed atoms for identity
bool SameTypedAtoms(TypedAtom a1, TypedAtom a2);

/**
 * Ordering of two typed atoms
 */
int8 CompareTypedAtoms(TypedAtom atom1, TypedAtom atom2);

/**
 * Sort a list of atoms in-place, ordered by CompareTypedAtoms()
 */
void SortTypedAtoms(TypedAtom * atoms, size32 nAtoms);

/**
 * Reduce a list of tyoed atoms in-place so that each atom occurs only once,
 * assuming that any duplicated atoms are adjacent in the array.
 * Writes the multiplicities of each atom to the
 * provided multiplicities array and returns the number of unique atoms.
 */
size8 ReduceTypedAtomsArray(TypedAtom * atoms, uint32 * multiplicities, size8 nAtoms);

/**
 * Pretty-print an atom to stdout.
 */
void PrintTypedAtom(TypedAtom a);


#endif	// TYPEDATOM_H
