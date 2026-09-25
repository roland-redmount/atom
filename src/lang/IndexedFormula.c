
#include "lang/formula.h"
#include "lang/IndexedFormula.h"
#include "lang/TermMultiset.h"
#include "memory/allocator.h"


IndexedFormula * CreateIndexedFormula(Atom form, TypedTuple * actors)
{
	IndexedFormula * indexedFormula = Allocate(sizeof(IndexedFormula));
	indexedFormula->form = form;
	indexedFormula->actors = actors;
	// compute indices
	uint8 nTerms = TermMultisetNTerms(form);
	indexedFormula->termActorsIndices = Allocate(nTerms + 1);
	TermMultisetGetTermActorsIndices(form, indexedFormula->termActorsIndices);

	return indexedFormula;
}


size8 IndexedFormulaTermArity(IndexedFormula const * indexedFormula, index8 i)
{
	return indexedFormula->termActorsIndices[i + 1]
		- indexedFormula->termActorsIndices[i];
}


TypedAtom IndexedFormulaGetTermElement(IndexedFormula const * indexedFormula, index8 i, index8 j)
{
	return TypedTupleGetElement(
		indexedFormula->actors,	indexedFormula->termActorsIndices[i] + j);
}


Atom IndexedFormulaGetTermAtom(IndexedFormula const * indexedFormula, index8 i, index8 j)
{
	return TypedTupleGetAtom(
		indexedFormula->actors,	indexedFormula->termActorsIndices[i] + j);
}


void IndexedFormulaSetTermElement(
	IndexedFormula const * indexedFormula, index8 i, index8 j, TypedAtom element)
{
	TypedTupleSetElement(
		indexedFormula->actors,	indexedFormula->termActorsIndices[i] + j, element);
}


void IndexedFormulaSetTermAtom(
	IndexedFormula const * indexedFormula, index8 i, index8 j, Atom atom)
{
	TypedTupleSetAtom(
		indexedFormula->actors,	indexedFormula->termActorsIndices[i] + j, atom);
}


Atom const * IndexedFormulaPeekTermAtoms(IndexedFormula const * indexedFormula, index8 i)
{
	return TypedTuplePeekAtoms(indexedFormula->actors) + indexedFormula->termActorsIndices[i];
}


TypedTuple * IndexedFormulaGetTermTuple(IndexedFormula const * indexedFormula, index8 i)
{
	size8 arity = IndexedFormulaTermArity(indexedFormula, i);
	TypedTuple * tuple = CreateTypedTuple(arity);
	TypedTupleCopyAt(indexedFormula->actors, indexedFormula->termActorsIndices[i], tuple);
	return tuple;
}


void PrintIndexedFormula(IndexedFormula const * indexedFormula)
{
	PrintFormActorsAsFormula(indexedFormula->form, indexedFormula->actors);
}


void FreeIndexedFormula(IndexedFormula const * indexedFormula)
{
	Free(indexedFormula->termActorsIndices);
	Free(indexedFormula);
}
