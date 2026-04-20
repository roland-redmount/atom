#include "lang/Variable.h"
#include "kernel/tuples.h"
#include "lang/TypedAtom.h"
#include "util/sort.h"


int8 CompareTuples(TypedAtom const * tuple1, TypedAtom const * tuple2, size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++) {
		int atomOrdering = CompareTypedAtoms(tuple1[i], tuple2[i]);
		if(atomOrdering < 0)
			return -1;
		if(atomOrdering > 0)
			return 1;
	}
	return 0;
}


bool SameTuples(TypedAtom const * source, TypedAtom * dest, size8 nAtoms)
{
    // NOTE: this could just be a memcmp over the entire block
	for(index8 i = 0; i < nAtoms; i++) {
		if(!SameTypedAtoms(dest[i], source[i]))
			return false;
	}
	return true;
}


void CopyTuples(TypedAtom const * source, TypedAtom * dest, size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++)
		dest[i] = source[i];
}

// comparator function for QuickSort
static int8 qsortCompareTuples(void const * tuple1, void const * tuple2, size32 tupleSize)
{
    size8 nAtoms = tupleSize / sizeof(TypedAtom);
    return CompareTuples((TypedAtom const *) tuple1, (TypedAtom const *) tuple2, nAtoms);
}

/**
 * NOTE: sorting order for tuples should be identical to the iteration order
 * of RelationBTree, which also uses CompareTuples for ordering tuples
 */
void SortTuples(void * tuples, size32 nTuples, size8 tupleNAtoms)
{
    QuickSort(tuples, nTuples, tupleNAtoms * sizeof(TypedAtom), qsortCompareTuples);
}


void PrintTuple(TypedAtom const * tuple, size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++) {
		PrintTypedAtom(tuple[i]);
		if(i < nAtoms - 1)
			PrintChar(' ');
	}
}


bool TupleMatch(TypedAtom const * tuple, TypedAtom const * queryTuple, size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++) {
		TypedAtom queryAtom = queryTuple[i];
		if(queryAtom.type == AT_VARIABLE) {
			if(VariableIsQuoted(queryAtom)) {
				// If the query variable is quoted, we remove the (outermost) quote.
				// This allows querying for a variable _x stored in a relation (foo)
				// using (foo '_x')
				// TODO: review the semantics of this!
				queryAtom = UnquoteVariable(queryAtom);
			}
			else {
				// variable matches any atom
				// verify that any repeated variables match identical atoms
				for(index8 j = i + 1; j < nAtoms; j++) {
					if(SameVariable(queryAtom, queryTuple[j])) {
						if(!SameTypedAtoms(tuple[i], tuple[j]))
							return false;
					}
				}
				continue;
			}
		}
		if(!SameTypedAtoms(queryAtom, tuple[i]))
			return false;
	}
	return true;
}


bool TupleContainsAtom(TypedAtom const * tuple, size8 nAtoms, TypedAtom atom)
{
	for(index8 i = 0; i < nAtoms; i++) {
		if(SameTypedAtoms(tuple[i], atom))
			return true;
	}
	return false;
}
