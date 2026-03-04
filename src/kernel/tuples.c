#include "datumtypes/Variable.h"
#include "kernel/tuples.h"
#include "lang/Atom.h"
#include "util/sort.h"


int8 CompareTuples(Atom const * tuple1, Atom const * tuple2, size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++) {
		int atomOrdering = CompareAtoms(tuple1[i], tuple2[i]);
		if(atomOrdering < 0)
			return -1;
		if(atomOrdering > 0)
			return 1;
	}
	return 0;
}


bool SameTuples(Atom const * source, Atom * dest, size8 nAtoms)
{
    // NOTE: this could just be a memcmp over the entire block
	for(index8 i = 0; i < nAtoms; i++) {
		if(!SameAtoms(dest[i], source[i]))
			return false;
	}
	return true;
}


void CopyTuples(Atom const * source, Atom * dest, size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++)
		dest[i] = source[i];
}

// comparator function for QuickSort
static int8 qsortCompareTuples(void const * tuple1, void const * tuple2, size32 tupleSize)
{
    size8 nAtoms = tupleSize / sizeof(Atom);
    return CompareTuples((Atom const *) tuple1, (Atom const *) tuple2, nAtoms);
}

/**
 * NOTE: sorting order for tuples should be identical to the iteration order
 * of RelationBTree, which also uses CompareTuples for ordering tuples
 */
void SortTuples(void * tuples, size32 nTuples, size8 tupleNAtoms)
{
    QuickSort(tuples, nTuples, tupleNAtoms * sizeof(Atom), qsortCompareTuples);
}


void PrintTuple(Atom const * tuple, size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++) {
		PrintAtom(tuple[i]);
		if(i < nAtoms - 1)
			PrintChar(' ');
	}
}


bool TupleMatch(Atom const * tuple, Atom const * queryTuple, size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++) {
		Atom queryAtom = queryTuple[i];
		if(queryAtom.type == DT_VARIABLE) {
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
						if(!SameAtoms(tuple[i], tuple[j]))
							return false;
					}
				}
				continue;
			}
		}
		if(!SameAtoms(queryAtom, tuple[i]))
			return false;
	}
	return true;
}


bool TupleContainsAtom(Atom const * tuple, size8 nAtoms, Atom atom)
{
	for(index8 i = 0; i < nAtoms; i++) {
		if(SameAtoms(tuple[i], atom))
			return true;
	}
	return false;
}
