#include "kernel/tuple.h"
#include "lang/TypedAtom.h"

void TupleCopy(Atom const sourceTuple[], Atom destinationTuple[], size8 nAtoms)
{
    CopyMemory(sourceTuple, destinationTuple, nAtoms * sizeof(Atom));
}


void TupleCopyPermuted(
	Atom const sourceTuple[], Atom destinationTuple[], index8 const permutation[], size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++)
		destinationTuple[permutation[i]] = sourceTuple[i];
}


int8 TupleCompare(Atom const tuple1[], Atom const tuple2[], size8 nAtoms)
{
    for(index8 i = 0; i < nAtoms; i++) {
        int8 atomOrder = CompareAtoms(tuple1[i], tuple2[i]);
        if(atomOrder != 0)
            return atomOrder;
    }
    return 0;
}

int8 TupleCompareInOrder(
    Atom const tuple1[], Atom const tuple2[], index8 const indexOrder[], size8 nAtoms)
{
    for(index8 i = 0; i < nAtoms; i++) {
        index8 index = indexOrder[i];
        int8 atomOrder = CompareAtoms(tuple1[index], tuple2[index]);
        if(atomOrder != 0)
            return atomOrder;
    }
    return 0;
}


bool TupleEqual(Atom const tuple1[], Atom const tuple2[], size8 nAtoms)
{
	return CompareMemory(tuple1, tuple2, nAtoms * sizeof(Atom)) == 0;
}


void TupleAcquire(byte const atomTypes[], Atom const tuple[], size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++)
		AcquireAtom(tuple[i], atomTypes[i]);
}


void TupleRelease(byte const atomTypes[], Atom const tuple[], size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++)
		ReleaseAtom(tuple[i], atomTypes[i]);
}


void PrintTuple(byte const atomTypes[], Atom const tuple[], size8 nAtoms)
{
   for(index8 i = 0; i < nAtoms; i++) {
        PrintTypedAtom(CreateTypedAtom(atomTypes[i], tuple[i]));
        if(i < nAtoms - 1)
            PrintChar(' ');
   }
}
