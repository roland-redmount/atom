/**
 * Some functions for operating on tuples, defines as plain C arrays of Atom.
  */

 #ifndef TUPLES_H
 #define TUPLES_H

#include "lang/TypedAtom.h"

void CopyTuples(TypedAtom const * source, TypedAtom * dest, size8 nAtoms);
bool SameTuples(TypedAtom const * source, TypedAtom * dest, size8 nAtoms);

/**
 * Canonical ordering of tuples
 */
int8 CompareTuples(TypedAtom const * tuple1, TypedAtom const * tuple2, size8 nAtoms);

void SortTuples(void * tuples, size32 nTuples, size8 tupleNAtoms);

void PrintTuple(TypedAtom const * tuple, size8 nAtoms);

/**
 * Test whether the tuple matches the query tuple, accounting for variables.
 * 
 * TODO: this does not handle queries with multiplicities as it does not account
 * for permutations; for example the query (a x_ a 1) will not match the fact (a 1 a 2)
 */
bool TupleMatch(TypedAtom const * tuple, TypedAtom const * queryTuple, size8 nAtoms);

bool TupleContainsAtom(TypedAtom const * tuple, size8 nAtoms, TypedAtom atom);

#endif  // TUPLES_H
