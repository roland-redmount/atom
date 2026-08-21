/**
 * A TypedTuple is an array of typed atoms, corresponding to actors in a formula.
 * It does not keep references to the atoms they contain.
 * This structure is intended for transient storage of tuples in the kernel.
  */

 #ifndef TYPEDTUPLE_H
 #define TYPEDTUPLE_H

#include "lang/TypedAtom.h"

typedef struct s_TypedTuple {
	size8 nAtoms;
	size8 reserved;
	/**
	 * We store the types and atoms array in a single contiguous block of memory.
	 * This creates a lot of overhead code for addressing items; it seemed motivated
	 * for efficiency when relation tables stored TypedTuple, but that is no longer true
	 * when TypedTuple is mainly used for transient copies. We should simplify this.
	 */ 
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
TypedTuple * CreateTypedTupleFromArray(TypedAtom const * typedAtoms, size8 nAtoms);

/**
 * Create a tuple by copying another tuple
 */
TypedTuple * CreateTupleFromTuple(TypedTuple const * otherTuple);

/**
 * Initialize a given memory block as a tuple.
 * NOTE: this is only used by IFactAddTuple(), can we remove?
 */
void SetupTypedTuple(TypedTuple * tuple, size8 nAtoms);

/**
 * Deallocate a types tuple.
 */
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
 * Return the tuple's atom array
 */
Atom const * TypedTuplePeekAtoms(TypedTuple const * tuple);


/**
 * Return the tuple's atom types array
 */
byte const * TypedTuplePeekAtomTypes(TypedTuple const * tuple);

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
void TypedTupleAcquireElements(TypedTuple const * tuple);

/**
 * Release all elements of the given tuple.
 */
void TypedTupleReleaseElements(TypedTuple const * tuple);

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
 * Hash value of a tuple.
 * The protected atom column does not influence the hash.
*/
data64 TypedTupleHash(TypedTuple const * tuple, data64 initialHash);

/**
 * Print a tuple
 */
void TypedTuplePrint(TypedTuple const * tuple);

/**
 * Test the tuple contains the given atom.
 */
bool TypedTupleContainsAtom(TypedTuple const * tuple, TypedAtom atom);

/**
 * Test if any atom of the tuple is a variable (AT_VARIABLE).
 */
bool TypedTupleContainsVariable(TypedTuple const * tuple);


#endif  // TYPEDTUPLE_H
