
#include "datumtypes/FloatIEEE754.h"
#include "datumtypes/Int.h"
#include "datumtypes/instruction.h"
#include "datumtypes/Parameter.h"
#include "datumtypes/UInt.h"
#include "datumtypes/Unknown.h"
#include "datumtypes/Variable.h"

#include "lang/name.h"
#include "lang/Quote.h"

#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/pair.h"
#include "kernel/string.h"

#include "lang/Atom.h"
#include "lang/ClauseForm.h"
#include "lang/Datum.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"

#include "util/hashing.h"
#include "util/sort.h"

// global constant invalid atom
Atom invalidAtom = {0, 0, 0, 0, 0};

/**
 * Create atom
 */
/*
Atom CreateAtom(DatumType type, TypePredicate predicate, data64 datum)
{
	return (Atom) {type, predicate, 0, 0, datum};
}
*/

void AcquireAtom(Atom atom)
{
	if(atom.type == DT_ID)
		IFactAcquire(atom);
	else if(atom.type == DT_NAME)
		NameAcquire(atom);
}


void ReleaseAtom(Atom atom)
{
	if(atom.type == DT_ID)
		IFactRelease(atom);
	else if(atom.type == DT_NAME)
		NameRelease(atom);
}


/**
 * Test for the invalid atom
 */
bool ValidAtomQ(Atom a)
{
	return a.type != 0;
}


/**
 * Compare two atoms for identity
 */
bool SameAtoms(Atom a1, Atom a2)
{
	return (a1.type == a2.type) && (a1.datum == a2.datum);
}


/**
 * Canonical ordering of atoms. This is used by CompareTuples()
 * which is used to order tuple storage in RelationBTree
 * 
 * NOTE: this orders atoms by the datum 64-bit value, which means
 * that IFacts including strings will be ordered by address.
 */
int8 CompareAtoms(Atom atom1, Atom atom2)
{
	// first order by datum type
	if(atom1.type < atom2.type)
		return -1;
	if(atom1.type > atom2.type)
		return 1;
	// for atoms of same type, order by datum
	return CompareDatums(atom1.datum, atom2.datum);
}


static int8 quickSortCompareAtoms(void const * item1, void const * item2, size32 itemSize)
{
	ASSERT(itemSize = sizeof(Atom));
	Atom atom1 = *((const Atom *) item1);
	Atom atom2 = *((const Atom *) item2);
	return CompareAtoms(atom1, atom2);
}


void SortAtoms(Atom * atoms, size32 nAtoms)
{
	QuickSort(atoms, nAtoms, sizeof(Atom), quickSortCompareAtoms);
}


static void shiftAtomArrayLeft(Atom * array, uint8 nAtoms, uint8 steps)
{
	for(index8 i = 0; i < nAtoms - steps; i++)
		array[i] = array[i + steps];
}


/**
 * Reduce a list of atoms in-place so that each atom occurs only once,
 * assuming that any duplicated atoms are adjacent in the array.
 * Writes the multiplicities of each datum to the
 * provided multiplicities array and returns the number of unique datums.
 */
size8 ReduceAtomArray(Atom * atoms, uint32 * multiplicities, size8 nAtoms)
{
	for(index8 k = 0; k < nAtoms; k++) {
		index8 i = k + 1;
		while((i < nAtoms) && SameAtoms(atoms[k], atoms[i]))
			i++;
		multiplicities[k] = i - k;
		if(multiplicities[k] > 1) {
			shiftAtomArrayLeft(atoms + k, nAtoms - k, multiplicities[k] - 1);
			nAtoms -= (multiplicities[k] - 1);
		}
	}
	return nAtoms;
}

/**
 * Print atom to stdout
 * This calls the datum print function for the datum type, wraps in [ ]
 */
void PrintAtom(Atom atom)
{
	// PrintChar('[');
	switch(atom.type) {
	case 0:
		// for debugging
		PrintCString("INVALID");
		break;

	case DT_UNKNOWN:
		PrintUnknown();
		break;

	case DT_UINT:
		PrintUInt(atom);
		break;

	case DT_INT:
		PrintInt(atom);
		break;

	case DT_FLOAT32:
		PrintFloat32(atom);
		break;

	case DT_FLOAT64:
		PrintFloat64(atom);
		break;

	case DT_LETTER:
		PrintLetter(atom, LETTER_UPPERCASE);
		break;

	case DT_VARIABLE:
		PrintVariable(atom);
		break;

	case DT_NAME:
		PrintName(atom);
		break;

	case DT_INSTRUCTION:
		PrintInstruction(atom);
		break;

	case DT_PARAMETER:
		PrintParameter(atom);
		break;

	case DT_ID:
		// for IFacts, string representation depends on the type predicate.
		// a given atom may satisfy multiple type predicates and therefore have multiple
		// string representations, so there is no straightforward switch/case.
		// Here we somewhat arbitrarily try the "most specific" type predicate first
		if(IsPair(atom)) {
			if(IsFormula(atom))
				PrintFormula(atom);
			else if(IsQuote(atom))
				PrintQuoted(atom);
			else
				PrintPair(atom);
		}
		else if(IsList(atom)) {
			if(IsString(atom))
				PrintString(atom);
			else if(IsName(atom))
				PrintName(atom);
			else
				PrintList(atom);
		}
		else if(IsMultiset(atom)) {
			if(IsPredicateForm(atom))
				PrintPredicateForm(atom);
			else if(IsTermForm(atom))
				PrintTermForm(atom);
			else if(IsClauseForm(atom))
				PrintClauseForm(atom);
			else
				PrintMultiset(atom);
		}
		else
			PrintIFact(atom);
		break;

	default:
		PrintF("ERROR: No Print method for datum type %u\n", atom.type);
		ASSERT(false);
	}
	// PrintChar(']');
}
