#include "lang/Variable.h"
#include "kernel/typedtuple.h"
#include "lang/TypedAtom.h"
#include "memory/allocator.h"
#include "util/hashing.h"
#include "util/sort.h"


/**
 * These functions give pointers to a tuple's type or atom arrays.
 * We have two versions for each to maintain const correctness.
 */
static byte * tupleTypeArray(TypedTuple * tuple)
{
	return ((byte *) tuple) + sizeof(TypedTuple);
}

byte const * TypedTuplePeekAtomTypes(TypedTuple const * tuple)
{
	return ((byte const *) tuple) + sizeof(TypedTuple);
}


static size32 tupleAtomArrayOffset(size8 tupleNAtoms)
{
	// size of the struct + types array, rounded up to an 8-byte boundary
	return ((sizeof(TypedTuple) + tupleNAtoms) + 7) & ~7;
}

static Atom * tupleAtomArray(TypedTuple * tuple)
{
	return (Atom *) (((byte *) tuple) + tupleAtomArrayOffset(tuple->nAtoms));
}


Atom const * TypedTuplePeekAtoms(TypedTuple const * tuple)
{
	return (Atom const *) (((byte const *) tuple) + tupleAtomArrayOffset(tuple->nAtoms));
}


size32 TypedTupleNBytes(size8 tupleNAtoms)
{
	return tupleAtomArrayOffset(tupleNAtoms) + tupleNAtoms * sizeof(Atom);
}


size8 TypedTupleNAtoms(size32 tupleNBytes)
{
	// size must be divisible by 8
	ASSERT((tupleNBytes >= 16) && !(tupleNBytes & 7))
	/**
	 * Inverting the formula is a bit complicated.
	 * Let s be the size in bytes and n the number of atoms.
	 * We have s = ceil((n+2)/8) + n = floor((n+9)/8) + n
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


// Acquire one reference to each of the nAtoms elements from the given index.
static void acquireElementRange(TypedTuple const * tuple, index8 index, size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++)
		AcquireTypedAtom(TypedTupleGetElement(tuple, index + i));
}


static void releaseElements(TypedTuple const * tuple)
{
	for(index8 i = 0; i < tuple->nAtoms; i++)
		ReleaseTypedAtom(TypedTupleGetElement(tuple, i));
}


TypedTuple * CreateTypedTuple(size8 nAtoms)
{
	// Allocate returns cleared memory, so every atom has type 0.
	TypedTuple * tuple = Allocate(TypedTupleNBytes(nAtoms));
	tuple->nAtoms = nAtoms;
	return tuple;
}


TypedTuple * CreateTypedTupleFromArray(TypedAtom const typedAtoms[], size8 nAtoms)
{
	TypedTuple * tuple = CreateTypedTuple(nAtoms);
	for(index8 i = 0; i < nAtoms; i++)
		TypedTupleSetElement(tuple, i, typedAtoms[i]);
	return tuple;
}


TypedTuple * CreateTupleFromTuple(TypedTuple const * otherTuple)
{
	TypedTuple * tuple = CreateTypedTuple(otherTuple->nAtoms);
	TypedTupleCopy(otherTuple, tuple);
	return tuple;
}


void FreeTypedTuple(TypedTuple const * tuple)
{
	releaseElements(tuple);
	Free(tuple);
}


TypedAtom TypedTupleGetElement(TypedTuple const * tuple, index8 index)
{
	byte type = TypedTuplePeekAtomTypes(tuple)[index];
	Atom atom = TypedTuplePeekAtoms(tuple)[index];
	return CreateTypedAtom(type, atom);
}

Atom TypedTupleGetAtom(TypedTuple const * tuple, index8 index)
{
	return TypedTuplePeekAtoms(tuple)[index];
}


void TypedTupleSetElement(TypedTuple * tuple, index8 index, TypedAtom element)
{
	// The incoming element is acquired before the outgoing one is released, so that
	// writing an element over itself cannot drop its last reference
	AcquireTypedAtom(element);
	ReleaseTypedAtom(TypedTupleGetElement(tuple, index));
	tupleTypeArray(tuple)[index] = element.type;
	tupleAtomArray(tuple)[index] = element.atom;
}


void TypedTupleSetAtom(TypedTuple * tuple, index8 index, Atom atom)
{
	// The element keeps its type, so both atoms are referenced under that type
	byte type = TypedTuplePeekAtomTypes(tuple)[index];
	AcquireAtom(atom, type);
	ReleaseAtom(TypedTuplePeekAtoms(tuple)[index], type);
	tupleAtomArray(tuple)[index] = atom;
}


int8 TypedTupleCompare(TypedTuple const * tuple1, TypedTuple const * tuple2)
{
	ASSERT(tuple1->nAtoms == tuple1->nAtoms)
	for(index8 i = 0; i < tuple1->nAtoms; i++) {
		int atomOrdering = CompareTypedAtoms(
			TypedTupleGetElement(tuple1, i), TypedTupleGetElement(tuple2, i));
		if(atomOrdering < 0)
			return -1;
		if(atomOrdering > 0)
			return 1;
	}
	return 0;
}


bool TypedTupleEqual(TypedTuple const * tuple1, TypedTuple const * tuple2)
{
	if(tuple1->nAtoms != tuple2->nAtoms)
		return false;
	// ignore the 
	return CompareMemory(
		TypedTuplePeekAtomTypes(tuple1),
		TypedTuplePeekAtomTypes(tuple2),
		TypedTupleNBytes(tuple1->nAtoms) - sizeof(TypedTuple)
	) == 0;
}


void TypedTupleCopy(TypedTuple const * source, TypedTuple * destination)
{
	ASSERT(source->nAtoms == destination->nAtoms)
	// As in TypedTupleSetElement(), the incoming elements are acquired first, so that
	// copying a tuple onto itself cannot drop the last reference to an element
	acquireElementRange(source, 0, source->nAtoms);
	releaseElements(destination);
	CopyMemory(source, destination, TypedTupleNBytes(source->nAtoms));
}


void TypedTupleCopyAt(TypedTuple const * source, index8 sourceOffset, TypedTuple * destination)
{
	ASSERT(source->nAtoms >= sourceOffset + destination->nAtoms)
	acquireElementRange(source, sourceOffset, destination->nAtoms);
	releaseElements(destination);
	CopyMemory(
		TypedTuplePeekAtomTypes(source) + sourceOffset,
		tupleTypeArray(destination),
		destination->nAtoms
	);
	CopyMemory(
		TypedTuplePeekAtoms(source) + sourceOffset,
		tupleAtomArray(destination),
		destination->nAtoms * sizeof(Atom)
	);
}


data64 TypedTupleHash(TypedTuple const * tuple, data64 initialHash)
{
	// hash header
	TypedTuple headerCopy = *tuple;
	data64 hash = DJB2DoubleHashAdd(&headerCopy, sizeof(TypedTuple), initialHash);
	// hash the type and atom arrays
	return DJB2DoubleHashAdd(
		TypedTuplePeekAtomTypes(tuple), TypedTupleNBytes(tuple->nAtoms) - sizeof(TypedTuple), hash);
}


void TypedTuplePrint(TypedTuple const * tuple)
{
	PrintChar('{');
	for(index8 i = 0; i < tuple->nAtoms; i++) {
		PrintTypedAtom(TypedTupleGetElement(tuple, i));
		if(i < tuple->nAtoms - 1)
			PrintChar(' ');
	}
	PrintChar('}');
}


bool TypedTupleContainsAtom(TypedTuple const * tuple, TypedAtom atom)
{
	for(index8 i = 0; i < tuple->nAtoms; i++) {
		if(SameTypedAtoms(TypedTupleGetElement(tuple, i), atom))
			return true;
	}
	return false;
}


bool TypedTupleContainsVariable(TypedTuple const * tuple)
{
	for(index8 i = 0; i < tuple->nAtoms; i++) {
		if(TypedTupleGetElement(tuple, i).type == AT_VARIABLE)
			return true;
	}
	return false;
}
