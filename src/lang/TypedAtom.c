
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
#include "lang/Atom.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"

#include "util/hashing.h"
#include "util/sort.h"
#include "vm/bytecode.h"


// global constant invalid atom
TypedAtom invalidAtom = {0};

/**
 * Create (typed) atom
 */

TypedAtom CreateTypedAtom(byte type, Atom atom)
{
	return (TypedAtom) {.type = type, .atom = atom};
}


void AcquireTypedAtom(TypedAtom typedAtom)
{
	switch(typedAtom.type) {
		case AT_ID:
		IFactAcquire(typedAtom.atom);
		break;

		case AT_NAME:
		NameAcquire(typedAtom.atom);
		break;
	}
}


void ReleaseTypedAtom(TypedAtom typedAtom)
{
	switch(typedAtom.type) {
		case AT_ID:
		IFactRelease(typedAtom.atom);
		break;

		case AT_NAME:
		NameRelease(typedAtom.atom);
		break;
	}
}


/**
 * Compare two atoms for identity
 */
bool SameTypedAtoms(TypedAtom typedAtom1, TypedAtom typedAtom2)
{
	return (typedAtom1.type == typedAtom2.type) && (typedAtom1.atom == typedAtom2.atom);
}


/**
 * Canonical ordering of typed atoms. This is used by CompareTuples()
 * which is used to order tuple storage in RelationBTree
 * 
 * NOTE: this orders atoms by the atom 64-bit value, which means
 * that IFacts including strings will be ordered by address.
 */
int8 CompareTypedAtoms(TypedAtom typedAtom1, TypedAtom typedAtom2)
{
	// first order by atom type
	if(typedAtom1.type < typedAtom2.type)
		return -1;
	if(typedAtom1.type > typedAtom2.type)
		return 1;
	// same type, order by atom alue
	return CompareAtoms(typedAtom1.atom, typedAtom2.atom);
}


static int8 quickSortCompareAtoms(void const * item1, void const * item2, size32 itemSize)
{
	ASSERT(itemSize = sizeof(TypedAtom));
	TypedAtom typedAtom1 = *((const TypedAtom *) item1);
	TypedAtom typedAtom2 = *((const TypedAtom *) item2);
	return CompareTypedAtoms(typedAtom1, typedAtom2);
}


void SortTypedAtoms(TypedAtom * typedAtoms, size32 nAtoms)
{
	QuickSort(typedAtoms, nAtoms, sizeof(TypedAtom), quickSortCompareAtoms);
}


static void shiftAtomArrayLeft(TypedAtom * typedAtoms, uint8 nAtoms, uint8 steps)
{
	for(index8 i = 0; i < nAtoms - steps; i++)
		typedAtoms[i] = typedAtoms[i + steps];
}


size8 ReduceTypedAtomsArray(TypedAtom * typedAtoms, uint32 * multiplicities, size8 nAtoms)
{
	for(index8 k = 0; k < nAtoms; k++) {
		index8 i = k + 1;
		while((i < nAtoms) && SameTypedAtoms(typedAtoms[k], typedAtoms[i]))
			i++;
		multiplicities[k] = i - k;
		if(multiplicities[k] > 1) {
			shiftAtomArrayLeft(typedAtoms + k, nAtoms - k, multiplicities[k] - 1);
			nAtoms -= (multiplicities[k] - 1);
		}
	}
	return nAtoms;
}

/**
 * Print atom to stdout
 * This calls the atom print function for the atom type, wraps in [ ]
 */
void PrintTypedAtom(TypedAtom typedAtom)
{
	// PrintChar('[');
	switch(typedAtom.type) {
	case AT_NONE:
		PrintCString("NONE");
		break;

	case AT_UINT:
		PrintUInt(typedAtom);
		break;

	case AT_INT:
		PrintInt(typedAtom);
		break;

	case AT_FLOAT32:
		PrintFloat32(typedAtom);
		break;

	case AT_FLOAT64:
		PrintFloat64(typedAtom);
		break;

	case AT_LETTER:
		PrintLetter(typedAtom, LETTER_UPPERCASE);
		break;

	case AT_VARIABLE:
		PrintVariable(typedAtom);
		break;

	case AT_NAME:
		PrintName(typedAtom.atom);
		break;

	case AT_INSTRUCTION:
		PrintInstruction(typedAtom.atom);
		break;

	case AT_PARAMETER:
		PrintParameter(typedAtom);
		break;

	case AT_ID:
		// for AT_ID, string representation depends on the type predicate.
		// a given atom may satisfy multiple type predicates and therefore have multiple
		// string representations, so there is no straightforward switch/case.
		// Here we somewhat arbitrarily try the "most specific" type predicate first
		
		// TODO: move this somewhere better
		if(IsPair(typedAtom.atom)) {
			if(IsFormula(typedAtom.atom))
				PrintFormula(typedAtom.atom);
			else if(IsQuote(typedAtom.atom))
				PrintQuoted(typedAtom.atom);
			else
				PrintPair(typedAtom.atom);
		}
		else if(IsList(typedAtom.atom)) {
			if(IsString(typedAtom.atom))
				PrintString(typedAtom.atom);
			else if(IsName(typedAtom))
				PrintName(typedAtom.atom);
			else
				PrintList(typedAtom.atom);
		}
		else if(IsMultiset(typedAtom.atom)) {
			if(IsPredicateForm(typedAtom.atom))
				PrintPredicateForm(typedAtom.atom);
			else if(IsTermForm(typedAtom.atom))
				PrintTermForm(typedAtom.atom);
			else if(IsClauseForm(typedAtom.atom))
				PrintClauseForm(typedAtom.atom);
			else
				PrintMultiset(typedAtom.atom);
		}
		else if(IsBytecode(typedAtom.atom))
			PrintBytecode(typedAtom.atom);
		else
			PrintIFact(typedAtom.atom);
		break;

	default:
		PrintF("ERROR: No Print method for atom type %u\n", typedAtom.type);
		ASSERT(false);
	}
	// PrintChar(']');
}
