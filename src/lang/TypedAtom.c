
#include "datumtypes/FloatIEEE754.h"
#include "datumtypes/Int.h"
#include "datumtypes/instruction.h"
#include "datumtypes/Parameter.h"
#include "datumtypes/UInt.h"
#include "datumtypes/Variable.h"

#include "lang/name.h"
#include "lang/Quote.h"

#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/pair.h"
#include "kernel/string.h"

#include "lang/TypedAtom.h"
#include "lang/ClauseForm.h"
#include "lang/Datum.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"

#include "util/hashing.h"
#include "util/sort.h"

// global constant invalid atom
TypedAtom invalidAtom = {0};

/**
 * Create (typed) atom
 */

TypedAtom CreateTypedAtom(byte type, Datum datum)
{
	return (TypedAtom) {.type = type, .datum = datum};
}


void AcquireTypedAtom(TypedAtom atom)
{
	if(atom.type == DT_ID)
		IFactAcquire(atom.datum);
	else if(atom.type == DT_NAME)
		NameAcquire(atom.datum);
}


void ReleaseTypedAtom(TypedAtom atom)
{
	if(atom.type == DT_ID)
		IFactRelease(atom.datum);
	else if(atom.type == DT_NAME)
		NameRelease(atom.datum);
}


/**
 * Compare two atoms for identity
 */
bool SameTypedAtoms(TypedAtom a1, TypedAtom a2)
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
int8 CompareTypedAtoms(TypedAtom atom1, TypedAtom atom2)
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
	ASSERT(itemSize = sizeof(TypedAtom));
	TypedAtom atom1 = *((const TypedAtom *) item1);
	TypedAtom atom2 = *((const TypedAtom *) item2);
	return CompareTypedAtoms(atom1, atom2);
}


void SortTypedAtoms(TypedAtom * atoms, size32 nAtoms)
{
	QuickSort(atoms, nAtoms, sizeof(TypedAtom), quickSortCompareAtoms);
}


static void shiftAtomArrayLeft(TypedAtom * array, uint8 nAtoms, uint8 steps)
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
size8 ReduceTypedAtomsArray(TypedAtom * atoms, uint32 * multiplicities, size8 nAtoms)
{
	for(index8 k = 0; k < nAtoms; k++) {
		index8 i = k + 1;
		while((i < nAtoms) && SameTypedAtoms(atoms[k], atoms[i]))
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
void PrintTypedAtom(TypedAtom atom)
{
	// PrintChar('[');
	switch(atom.type) {
	case DT_NONE:
		PrintCString("NONE");
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
		PrintName(atom.datum);
		break;

	case DT_INSTRUCTION:
		PrintInstruction(atom);
		break;

	case DT_PARAMETER:
		PrintParameter(atom);
		break;

	case DT_ID:
		// for DT_ID, string representation depends on the type predicate.
		// a given atom may satisfy multiple type predicates and therefore have multiple
		// string representations, so there is no straightforward switch/case.
		// Here we somewhat arbitrarily try the "most specific" type predicate first
		// TODO: move this to id.h
		if(IsPair(atom.datum)) {
			if(IsFormula(atom.datum))
				PrintFormula(atom.datum);
			else if(IsQuote(atom.datum))
				PrintQuoted(atom.datum);
			else
				PrintPair(atom.datum);
		}
		else if(IsList(atom.datum)) {
			if(IsString(atom.datum))
				PrintString(atom.datum);
			else if(IsName(atom))
				PrintName(atom.datum);
			else
				PrintList(atom.datum);
		}
		else if(IsMultiset(atom.datum)) {
			if(IsPredicateForm(atom.datum))
				PrintPredicateForm(atom.datum);
			else if(IsTermForm(atom.datum))
				PrintTermForm(atom.datum);
			else if(IsClauseForm(atom.datum))
				PrintClauseForm(atom.datum);
			else
				PrintMultiset(atom.datum);
		}
		else
			PrintIFact(atom.datum);
		break;

	default:
		PrintF("ERROR: No Print method for datum type %u\n", atom.type);
		ASSERT(false);
	}
	// PrintChar(']');
}
