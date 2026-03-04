/**
 * An Atom stores a 64-bit datum, identified by a 32-bit datum type.
 * See DatumType.h
 *
 * Atoms must serialized for transfer between data nodes (processes). The exact
 * serialized form is data type-dependent, but must include enough information
 * to uniquely identify the datum _within the context of the data node_.  
 * 
 * TODO:  I think this should be called TypedDatum to avoid confusion. Atom is a more high-level concept
 */

#ifndef ATOM_H
#define ATOM_H

#include "Datum.h"
#include "DatumType.h"

/**
 * Atom structure, 12 bytes
 * 
 * NOTE: we could make this a single 64-bit value if we're willing to lose
 * a few bits for the actual datum. x86 uses only 48 bits for addressing, so
 * we can easily fit pointer anyway. For integers we would lose a few bits.
 * Double precision floats would not work though. And it will require constant
 * marshalling of 64-bit numeric types in/out of atoms which is not appealing.
 * Refering to atoms with a single 64-bit word does seem nice though.
 * 
 * Hopefully we won't need the type information in bytecode computation
 * (already determined by dispatch) and so we'll deal only with the 64-bit datum.
 * Another option is to make atom sizes variable a'la SQLite ...
 */
typedef struct s_Atom
{
	byte type;
	byte flags;
	byte reserved1;
	byte reserved2;
	Datum datum;					// the 64-bit datum
} __attribute__((packed)) Atom;


// flags
#define ATOM_PROTECTED		1	// used in tuples storing ifacts to mark the identified atom
								// NOTE: this should be internal to RelationBTree,
								// as it only affect btree/ifact storage logic

// the invalid atom, only used internally to signal errors
extern Atom invalidAtom;
// the "unknown" atom
extern Atom unknownAtom;

// reference handling
void AcquireAtom(Atom atom);
void ReleaseAtom(Atom atom);

// test if a is the invalid atom (used to check for errors)
bool ValidAtomQ(Atom a);

// compare two atoms for identity
bool SameAtoms(Atom a1, Atom a2);

// ordering of two atoms
int8 CompareAtoms(Atom atom1, Atom atom2);

/**
 * Sort a list of atoms in-place, ordered by CompareAtoms()
 */
void SortAtoms(Atom * atoms, size32 nAtoms);

// NOTE: this shold probably go elsewhere
size8 ReduceAtomArray(Atom * atoms, uint32 * multiplicities, size8 nAtoms);

/**
 * Pretty-print an atom to stdout.
 * TODO: replace with action print
 */
void PrintAtom(Atom a);


#endif	// ATOM_H
