/**
 * A tuple is an array of typed atoms, corresponding to actors in a formula.
 * Tuples do not keep references to the atoms they contain.
  */

 #ifndef TUPLE_H
 #define TUPLE_H

#include "lang/TypedAtom.h"

typedef struct s_Tuple {
	size8 nAtoms;
	// 1-based position of the protected atom, or 0 if none
	// a protected atom occurs only in tuples that are part of the IFact
	index8 protectedAtom;
	// byte types[]
	// Atom atoms[]
} __attribute__((packed)) Tuple;

// Tuple * CreateTuple


size32 TupleNBytes(size8 tupleNAtoms);

size8 TupleNAtoms(size32 tupleNBytes);

/**
 * Create an empty tuple (all atoms zero)
 */
Tuple * CreateTuple(size8 nAtoms);

/**
 * Create a tuple by copying an array of atoms
 */
Tuple * CreateTupleFromArray(TypedAtom * typedAtoms, size8 nAtoms);

/**
 * Create a tuple by copying another tuple
 */
Tuple * CreateTupleFromtuple(Tuple const * otherTuple);

void SetupTuple(Tuple * tuple, size8 nAtoms);


void FreeTuple(Tuple * tuple);

/**
 * Get the TypedAtom at the given index, 0-based
 */
TypedAtom TupleGetElement(Tuple const * tuple, index8 index);

/**
 * Get the (untyped) Atom at the given index, 0-based
 */
Atom TupleGetAtom(Tuple const * tuple, index8 index);

/**
 * Set the TypedAtom at the given index, 0-based
 */
void TupleSetElement(Tuple * tuple, index8 index, TypedAtom element);

/**
 * Set the (untyped) Atom at the given index, 0-based.
 * The type of the atom is unchanged.
 */
void TupleSetAtom(Tuple * tuple, index8 index, Atom atom);

/**
 * Copy the tuple's atoms into the given atom array
 */
void TupleGetAtoms(Tuple const * tuple, Atom * atoms);

/**
 * Copy the atoms array into the tuple's atom araray,
 * while leaving the atom types unchanges.
 */
void TupleSetAtoms(Tuple * tuple, Atom const* atoms);

bool TupleIsProtected(Tuple const * tuple);

/**
 * Copy one tuple into another. The destination tuple must
 * have been initialized and contain the same number of atoms
 * as the source tuple.
 */
void CopyTuples(Tuple const * source, Tuple * destination);

/**
 * Compare two tuples for equality.
 * The protectedAtom field is ignored for the comparison.
 */
bool SameTuples(Tuple const * tuple1, Tuple const * tuple2);

/**
 * Canonical ordering of tuples
 */
int8 CompareTuples(Tuple const * tuple1, Tuple const * tuple2);

/**
 * Sort a list of tuples
 */
void SortTuples(Tuple * tuples, size32 nTuples);

/**
 * Hash value of a tuple.
 * The protected atom column does not influence the hash.
*/
data64 TupleHash(Tuple const * tuple, data64 initialHash);

/**
 * Print a tuple
 */
void PrintTuple(Tuple const * tuple);

/**
 * Test whether the tuple matches the query tuple, accounting for variables.
 * 
 * TODO: this does not handle queries with multiplicities as it does not account
 * for permutations; for example the query (a x_ a 1) will not match the fact (a 1 a 2)
 */
bool TupleMatch(Tuple const * tuple, Tuple const * queryTuple);

bool TupleContainsAtom(Tuple const * tuple, TypedAtom atom);


#endif  // TUPLE_H
