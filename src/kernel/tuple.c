#include "lang/Variable.h"
#include "kernel/tuple.h"
#include "lang/TypedAtom.h"
#include "memory/allocator.h"
#include "util/hashing.h"
#include "util/sort.h"


static byte * tupleTypeArray(Tuple * tuple)
{
	return ((byte *) tuple) + sizeof(Tuple);
}


static size32 tupleAtomArrayOffset(size8 tupleNAtoms)
{
	// size of the struct + types array, rounded up to an 8-byte boundary
	return ((sizeof(Tuple) + tupleNAtoms) + 7) & ~7;
}


static Atom * tupleAtomArray(Tuple * tuple)
{
	return (Atom *) (((byte *) tuple) + tupleAtomArrayOffset(tuple->nAtoms));
}


size32 TupleNBytes(size8 tupleNAtoms)
{
	return tupleAtomArrayOffset(tupleNAtoms) + tupleNAtoms * sizeof(Atom);
}


size8 TupleNAtoms(size32 tupleNBytes)
{
	// size must be divisible by 8
	ASSERT((tupleNBytes >= 16) && !(tupleNBytes & 7))
	/**
	 * Inverting the formula is a bit complicated.
	 * 
	 * Let s = ceil((n+2)/8) + n = floor((n+9)/8) + n
	 * since ceil((n+2)/8) = floor((n+2+7)/8)
	 * 
	 * Express n as n = 8q + r. We get two cases:
	 * 1) r < 7,
	 *   s = floor((8q + r + 9) / 8) + 8q + r = q + 1 +  8q + r = 9q + r + 1
	 *   so s % 9 > 0 and q = floor(s/9), and r = s - 9q - 1
	 *   ==> n = 8q + r = 8*floor(s/9) + s - 9*floor(s/9) - 1
	 *                  = 8*floor(s/9) + s % 9 - 1
	 * 
	 * 2) r = 7,
	 *   s = floor((8q + 7 + 9) / 8) + 8q + 7 = q + 2 + 8q + 7 = 9q + 9
	 *   so s % 9 == 0 and s = 9(q+1) ==> q = s/9 - 1
	 *   ==>  n = 8q + r = 8*(s/9) - 1
	 *                   = 8*floor(s/9) + s % 9 - 1  (since s%9 == 0)
	 * 
	 * So n = 8*floor(s/9) + s % 9 - 1 is valid in both cases.
	 * 
	 */
	uint32 s = tupleNBytes >> 3;
	return 8 * (s / 9) + (s % 9) - 1;
}


Tuple * CreateTuple(size8 nAtoms)
{
	Tuple * tuple = Allocate(TupleNBytes(nAtoms));
	SetupTuple(tuple, nAtoms);
	return tuple;
}


Tuple * CreateTupleFromArray(TypedAtom * typedAtoms, size8 nAtoms)
{
	Tuple * tuple = CreateTuple(nAtoms);
	for(index8 i = 0; i < nAtoms; i++)
		TupleSetElement(tuple, i, typedAtoms[i]);
	return tuple;
}


Tuple * CreateTupleFromtuple(Tuple const * otherTuple)
{
	Tuple * tuple = CreateTuple(otherTuple->nAtoms);
	CopyTuples(otherTuple, tuple);
	return tuple;
}


void SetupTuple(Tuple * tuple, size8 nAtoms)
{
	SetMemory(tuple, TupleNBytes(nAtoms), 0);
	tuple->nAtoms = nAtoms;
}


void FreeTuple(Tuple * tuple)
{
	Free(tuple);
}


TypedAtom TupleGetElement(Tuple const * tuple, index8 index)
{
	byte type = tupleTypeArray((Tuple *) tuple)[index];
	Atom atom = tupleAtomArray((Tuple *) tuple)[index];
	return CreateTypedAtom(type, atom);
}


void TupleSetElement(Tuple * tuple, index8 index, TypedAtom element)
{
	tupleTypeArray(tuple)[index] = element.type;
	tupleAtomArray(tuple)[index] = element.atom;
}


void TupleGetAtoms(Tuple const * tuple, Atom * atoms)
{
	CopyMemory(tupleAtomArray((Tuple*) tuple), atoms, tuple->nAtoms * sizeof(Atom));
}


void TupleSetAtoms(Tuple * tuple, Atom const * atoms)
{
	CopyMemory(atoms, tupleAtomArray(tuple), tuple->nAtoms * sizeof(Atom));
}


bool TupleIsProtected(Tuple const * tuple)
{
	return tuple->protectedAtom > 0;
}


int8 CompareTuples(Tuple const * tuple1, Tuple const * tuple2)
{
	ASSERT(tuple1->nAtoms == tuple1->nAtoms)
	for(index8 i = 0; i < tuple1->nAtoms; i++) {
		int atomOrdering = CompareTypedAtoms(
			TupleGetElement(tuple1, i), TupleGetElement(tuple2, i));
		if(atomOrdering < 0)
			return -1;
		if(atomOrdering > 0)
			return 1;
	}
	return 0;
}


bool SameTuples(Tuple const * tuple1, Tuple const * tuple2)
{
	if(tuple1->nAtoms != tuple1->nAtoms)
		return false;
	// ignore the 
	return CompareMemory(
		tupleTypeArray((Tuple *) tuple1),
		tupleTypeArray((Tuple *) tuple2),
		TupleNBytes(tuple1->nAtoms) - sizeof(Tuple)
	) == 0;
}


void CopyTuples(Tuple const * source, Tuple * destination)
{
	ASSERT(source->nAtoms == destination->nAtoms)
	CopyMemory(source, destination, TupleNBytes(source->nAtoms));
}


data64 TupleHash(Tuple const * tuple, data64 initialHash)
{
	// hash header, except the protectedAtom field
	Tuple headerCopy = *tuple;
	headerCopy.protectedAtom = 0;	
	data64 hash = DJB2DoubleHashAdd(&headerCopy, sizeof(Tuple), initialHash);
	// hash the type and atom arrays
	return DJB2DoubleHashAdd(
		tupleTypeArray((Tuple *) tuple), TupleNBytes(tuple->nAtoms) - sizeof(Tuple), hash);
}


// ItemComparator function for QuickSort
static int8 quickSortCompareTuples(void const * tuple1, void const * tuple2, size32 tupleSize)
{
    return CompareTuples((Tuple const *) tuple1, (Tuple const *) tuple2);
}

/**
 * NOTE: sorting order for tuples should be identical to the iteration order
 * of RelationBTree; see compareQuery() in RelationBTree.c
 */
void SortTuples(Tuple * tuples, size32 nTuples)
{
	ASSERT(nTuples > 0)
    QuickSort(tuples, nTuples, TupleNBytes(tuples[0].nAtoms), quickSortCompareTuples);
}


void PrintTuple(Tuple const * tuple)
{
	PrintChar('{');
	for(index8 i = 0; i < tuple->nAtoms; i++) {
		PrintTypedAtom(TupleGetElement(tuple, i));
		if(i < tuple->nAtoms - 1)
			PrintChar(' ');
	}
	PrintChar('}');
}


bool TupleMatch(Tuple const * tuple, Tuple const * queryTuple)
{
	ASSERT(tuple->nAtoms == queryTuple->nAtoms)
	for(index8 i = 0; i < tuple->nAtoms; i++) {
		TypedAtom atom = TupleGetElement(tuple, i);
		TypedAtom queryAtom = TupleGetElement(queryTuple, i);
		if(queryAtom.type == AT_VARIABLE) {
			if(VariableIsQuoted(queryAtom)) {
				// If the query variable is quoted, we remove the (outermost) quote.
				// This allows querying for a variable _x stored in a relation (foo)
				// using (foo '_x')
				// TODO: review the semantics of this!
				queryAtom = UnquoteVariable(queryAtom);
			}
			else {
				// A query variable matches any atom, but
				// any repeats of this variable must correspond to the same atom
				for(index8 j = i + 1; j < tuple->nAtoms; j++) {
					TypedAtom nextQueryAtom = TupleGetElement(queryTuple, j);
					if((nextQueryAtom.type == AT_VARIABLE) && SameVariable(queryAtom, nextQueryAtom)) {
						TypedAtom nextAtom = TupleGetElement(tuple, j);
						if(!SameTypedAtoms(atom, nextAtom))
							return false;
					}
				}
				continue;
			}
		}
		else {
			// all other atoms must be equal
			if(!SameTypedAtoms(queryAtom, atom))
				return false;
		}
	}
	return true;
}


bool TupleContainsAtom(Tuple const * tuple, TypedAtom atom)
{
	for(index8 i = 0; i < tuple->nAtoms; i++) {
		if(SameTypedAtoms(TupleGetElement(tuple, i), atom))
			return true;
	}
	return false;
}
