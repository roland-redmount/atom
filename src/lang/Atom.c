
#include "kernel/ifact.h"
#include "lang/name.h"


int8 CompareAtoms(Atom atom1, Atom atom2)
{
	if(atom1.hash < atom2.hash)
		return -1;
	if(atom1.hash > atom2.hash)
		return 1;
	return 0;
}


static void shiftAtomsArrayLeft(Atom * atoms, uint8 nDatums, uint8 steps)
{
	for(index8 i = 0; i < nDatums - steps; i++)
		atoms[i] = atoms[i + steps];
}


static int8 quickSortCompareAtoms(void const * item1, void const * item2, size32 itemSize)
{
	ASSERT(itemSize = sizeof(Atom));
	Atom atom1 = *((const Atom *) item1);
	Atom atom2 = *((const Atom *) item2);
	return CompareAtoms(atom1, atom2);
}


void SortAtoms(Atom atoms[], size32 nAtoms)
{
	QuickSort(atoms, nAtoms, sizeof(TypedAtom), quickSortCompareAtoms);
}


/**
 * Reduce a list of atoms in-place so that each atom occurs only once,
 * assuming that any duplicated atoms are adjacent in the array.
 * Writes the multiplicities of each atom to the
 * provided multiplicities array and returns the number of unique atoms.
 */
uint8 ReduceAtomsArray(Atom * atoms, uint32 * multiplicities, size8 nAtoms)
{
	for(index8 k = 0; k < nAtoms; k++) {
		index8 i = k + 1;
		while((i < nAtoms) && (atoms[k].hash == atoms[i].hash))
			i++;
		multiplicities[k] = i - k;
		if(multiplicities[k] > 1) {
			shiftAtomsArrayLeft(atoms + k, nAtoms - k, multiplicities[k] - 1);
			nAtoms -= (multiplicities[k] - 1);
		}
	}
	return nAtoms;
}


void AcquireAtom(Atom atom, byte atomType)
{
	switch(atomType) {
		case AT_ID:
		IFactAcquire(atom);
		break;

		case AT_NAME:
		NameAcquire(atom);
		break;

		// else nothing to do
	}
}
