#include "kernel/tuple.h"
#include "lang/TypedAtom.h"

void TupleCopy(Atom const sourceTuple[], Atom destinationTuple[], size8 nAtoms)
{
    CopyMemory(sourceTuple, destinationTuple, nAtoms * sizeof(Atom));
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

bool TupleEqual(Atom const tuple1[], Atom const tuple2[], size8 nAtoms)
{
	return CompareMemory(tuple1, tuple2, nAtoms * sizeof(Atom)) == 0;
}


void PrintTuple(byte const atomTypes[], Atom const tuple[], size8 nAtoms)
{
   for(index8 i = 0; i < nAtoms; i++) {
        PrintTypedAtom(CreateTypedAtom(atomTypes[i], tuple[i]));
        if(i < nAtoms - 1)
            PrintChar(' ');
   }
}
