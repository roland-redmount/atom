
#include "lang/TermMultiset.h"
#include "lang/TermForm.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/Relation.h"


Atom CreateTermMultiset(Atom const termForms[], size8 nTermForms, index32 relationId)
{
	// reduce to unique terms
	Atom uniqueTermForms[nTermForms];
	CopyMemory(termForms, uniqueTermForms, nTermForms * sizeof(Atom));
	// CLAUDE: ReduceAtomsArray() requires repeated term forms to be adjacent
	SortAtoms(uniqueTermForms, nTermForms);
	uint32 multiplicities[nTermForms];
	size8 nUniqueTermForms = ReduceAtomsArray(uniqueTermForms, multiplicities, nTermForms);

	IFactDraft draft;
	IFactBegin(&draft);

	AddMultisetToIFactFromArrays(&draft, uniqueTermForms, multiplicities, nUniqueTermForms, AT_ID);

	// add the (clause-form) or (conjunction-form) tuple, depending on relationId
	TupleStore * store = GetCoreTupleStore(relationId);
	IFactBeginConjunction(&draft, store, 0);
	IFactAddTuple(&draft, (Atom[]) {(Atom) {0}});
	IFactEndConjunction(&draft);

	return IFactEnd(&draft);
}


size8 TermMultisetNUniqueTermForms(Atom form)
{
	return MultisetNUniqueElements(form, AT_ID);
}


size8 TermMultisetNTerms(Atom form)
{
	return MultisetSize(form, AT_ID);
}


size8 TermMultisetArity(Atom form)
{
	// the arity is the sum over unique terms of term arity times multiple
	MultisetIterator iterator;
	MultisetIterate(form, AT_ID, &iterator);
	size8 arity = 0;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		arity += TermFormArity(elementMultiple.element) * elementMultiple.multiple;
	}
	MultisetIteratorEnd(&iterator);
	return arity;
}


void TermMultisetGetTermActorsIndices(Atom form, index8 termActorsIndices[])
{
	// iterate over terms in the multiset and compute indices
	MultisetIterator iterator;
	MultisetIterate(form, AT_ID, &iterator);

	index8 k = 0;
	termActorsIndices[k] = 0;
	ElementMultiple elementMultiple;
	while(MultisetIteratorNext(&iterator)) {
		elementMultiple = MultisetIteratorGetElement(&iterator);
		size8 termArity = TermFormArity(elementMultiple.element);
		for(index8 i = 0; i < elementMultiple.multiple; i++) {
			termActorsIndices[k + 1] = termActorsIndices[k] + termArity;
			k++;
		}
	}
	MultisetIteratorEnd(&iterator);
}
