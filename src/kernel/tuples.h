/**
 * Some functions for operating on tuples, defines as plain C arrays of Atom.
  */

 #ifndef TUPLES_H
 #define TUPLES_H

#include "lang/Atom.h"

void CopyTuples(Atom const * source, Atom * dest, size8 nAtoms);
bool SameTuples(Atom const * source, Atom * dest, size8 nAtoms);

/**
 * Canonical ordering of tuples
 */
int8 CompareTuples(Atom const * tuple1, Atom const * tuple2, size8 nAtoms);

void SortTuples(void * tuples, size32 nTuples, size8 tupleNAtoms);

void PrintTuple(Atom const * tuple, size8 nAtoms);

/**
 * Test whether the tuple matches the query tuple, accounting for variables.
 * 
 * TODO: this does not handle queries with multiplicities as it does not account
 * for permutations; for example the query (a x_ a 1) will not match the fact (a 1 a 2)
 */
bool TupleMatch(Atom const * tuple, Atom const * queryTuple, size8 nAtoms);

bool TupleContainsAtom(Atom const * tuple, size8 nAtoms, Atom atom);

#endif  // TUPLES_H
