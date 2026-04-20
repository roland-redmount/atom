
#include "lang/Atom.h"


int8 CompareAtoms(Atom atom1, Atom atom2)
{
	if(atom1 < atom2)
		return -1;
	if(atom1 > atom2)
		return 1;
	return 0;
}


static void shiftAtomsArrayLeft(Atom * atoms, uint8 nDatums, uint8 steps)
{
	for(index8 i = 0; i < nDatums - steps; i++)
		atoms[i] = atoms[i + steps];
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
		while((i < nAtoms) && (atoms[k] == atoms[i]))
			i++;
		multiplicities[k] = i - k;
		if(multiplicities[k] > 1) {
			shiftAtomsArrayLeft(atoms + k, nAtoms - k, multiplicities[k] - 1);
			nAtoms -= (multiplicities[k] - 1);
		}
	}
	return nAtoms;
}
