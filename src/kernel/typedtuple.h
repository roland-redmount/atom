/**
 * A TypedTuple is an array of typed atoms, corresponding to actors in a formula.
 * Tuples do not keep references to the atoms they contain.
  */

 #ifndef TYPEDTUPLE_H
 #define TYPEDTUPLE_H

#include "lang/TypedAtom.h"

typedef struct s_TypedTuple {
	size8 nAtoms;
	/**
	 * 1-based position of the protected atom, or 0 if no atom is protected.
	 * a protected atom occurs only in tuples that are part of the IFact.
	 * 
	 * TODO: This does not seem to belong in a low-level construct like Tuple.
	 * Tuple storage methods like RelationBTree should not have to deal with
	 * the protected atom logic. Can we move the "protected" logic to lookup?
	 */ 
	// 
	index8 protectedAtom;
	// byte types[]
	// Atom atoms[]
} __attribute__((packed)) TypedTuple;


/**
 * Size in bytes of a TypedTuple with the given number of atoms.
 */

size32 TypedTupleNBytes(size8 tupleNAtoms);

/**
 * Number of atoms in a TypedTuple of the given size in bytes.
 */
size8 TypedTupleNAtoms(size32 tupleNBytes);

/**
 * Create an empty tuple (all atoms zero)
 */
TypedTuple * CreateTypedTuple(size8 nAtoms);

/**
 * Create a tuple by copying an array of atoms
 */
TypedTuple * CreateTypedTupleFromArray(TypedAtom * typedAtoms, size8 nAtoms);

/**
 * Create a tuple by copying another tuple
 */
TypedTuple * CreateTupleFromTuple(TypedTuple const * otherTuple);

/**
 * Initialize a given memory block as a tuple.
 * NOTE: this is only used by IFactAddTuple(), can we remove?
 */
void SetupTypedTuple(TypedTuple * tuple, size8 nAtoms);

void FreeTypedTuple(TypedTuple * tuple);

/**
 * Set all atoms to zero
 */
void TypedTupleClear(TypedTuple * tuple);

/**
 * Get the TypedAtom at the given index, 0-based
 */
TypedAtom TypedTupleGetElement(TypedTuple const * tuple, index8 index);

/**
 * Get the (untyped) Atom at the given index, 0-based
 */
Atom TypedTupleGetAtom(TypedTuple const * tuple, index8 index);

/**
 * Set the TypedAtom at the given index, 0-based
 */
void TypedTupleSetElement(TypedTuple * tuple, index8 index, TypedAtom element);

/**
 * Set the (untyped) Atom at the given index, 0-based.
 * The type of the atom is unchanged.
 */
void TypedTupleSetAtom(TypedTuple * tuple, index8 index, Atom atom);

/**
 * Copy untyped atoms from the TypedTuple into the given Atom array
 */
// void TypedTupleGetAtoms(TypedTuple const * tuple, Atom * atoms);

/**
 * Copy the atoms array into the tuple's atom array,
 * while leaving the atom types unchanges.
 */
// void TypedTupleSetAtoms(TypedTuple * tuple, Atom const* atoms);

/**
 * Get the atom type of a tuple element
 */
// byte TupleGetAtomType(TypedTuple const * tuple, index8 index);


/**
 * Copy one tuple into another. The destination tuple must
 * have been initialized and contain the same number of atoms
 * as the source tuple.
 */
void TypedTupleCopy(TypedTuple const * source, TypedTuple * destination);

/**
 * Copy each element i from the source tuple to element order[i] of the destination tuple.
 * The order array must have at least as many elements as the source and destination tuples.
 */
void TypedTupleCopyReorder(TypedTuple const * source, TypedTuple * destination, index8 const * order);

/**
 * Copy destination->nAtoms from the source tuple into the destination,
 * starting at the given offset (0-based index to first element).
 */

void TypedTupleCopyAt(TypedTuple const * source, index8 sourceOffset, TypedTuple * destination);

/**
 * Swap the contents of two tuples.
 */
void TypedTupleSwap(TypedTuple * tuple1, TypedTuple * tuple2);

/**
 * Acquire all elements of the given tuple.
 */
void TypedTupleAcquire(TypedTuple const * tuple);

/**
 * Release all elements of the given tuple.
 */
void TypedTupleRelease(TypedTuple const * tuple);

/**
 * Compare two tuples for equality.
 * The protectedAtom field is ignored for the comparison.
 */
bool TypedTupleEqual(TypedTuple const * tuple1, TypedTuple const * tuple2);

/**
 * Canonical ordering of tuples
 */
int8 TypedTupleCompare(TypedTuple const * tuple1, TypedTuple const * tuple2);

/**
 * Sort a list of tuples
 */
void TypedTupleSort(TypedTuple * tuples, size32 nTuples);

/**
 * Hash value of a tuple.
 * The protected atom column does not influence the hash.
*/
data64 TypedTupleHash(TypedTuple const * tuple, data64 initialHash);

/**
 * Print a tuple
 */
void TypedTuplePrint(TypedTuple const * tuple);

/**
 * Test whether the tuple matches the query tuple, accounting for variables.
 * 
 * TODO: this does not handle queries with multiplicities as it does not account
 * for permutations; for example the query (a x_ a 1) will not match the fact (a 1 a 2)
 */
bool TypedTupleMatch(TypedTuple const * tuple, TypedTuple const * queryTuple);

bool TypedTupleContainsAtom(TypedTuple const * tuple, TypedAtom atom);


#endif  // TYPEDTUPLE_H
