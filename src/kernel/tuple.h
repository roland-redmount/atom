/**
 * A Tuple consists of an Atom array and atom count.
 * NOTE: this might not be necesssary; can use Atom[] atoms, size8 nAtoms
 */
#ifndef TUPLE_H
#define TUPLE_H

#include "lang/Atom.h"

/**
 * Copy the source tuple contents to the destination tuple.
 */
void TupleCopy(Atom const sourceTuple[], Atom destinationTuple[], size8 nAtoms);

/**
 * Copy a tuple with permutation, so that
 * destinationTuple[permutation[i]] = sourceTuple[i]
 */
void TupleCopyPermuted(
	Atom const sourceTuple[], Atom destinationTuple[], index8 const permutation[], size8 nAtoms);

/**
 * Determine the ordering of two tuples
 */
int8 TupleCompare(Atom const tuple1[], Atom const tuple2[], size8 nAtoms);

/**
 * Compare two tuples lexiographically with respect to an index order, which is a
 * permutation of the tuple indices giving the significance of each atom: the atoms
 * at index indexOrder[0] are compared first, then those at indexOrder[1], and so on.
 * With the identity permutation this is TupleCompare().
 */
int8 TupleCompareInOrder(
	Atom const tuple1[], Atom const tuple2[], index8 const indexOrder[], size8 nAtoms);

/**
 * Compare two tuples for equality
 */
bool TupleEqual(Atom const tuple1[], Atom const tuple2[], size8 nAtoms);

/**
 * Acquire a reference to each atom of the tuple. Only atom types identifying
 * a shared object are reference counted; see AcquireAtom().
 */
void TupleAcquire(byte const atomTypes[], Atom const tuple[], size8 nAtoms);

/**
 * Remove one reference to each atom of the tuple.
 */
void TupleRelease(byte const atomTypes[], Atom const tuple[], size8 nAtoms);

void PrintTuple(byte const atomTypes[], Atom const tuple[], size8 nAtoms);

#endif	// TUPLE_H
