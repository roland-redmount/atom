
#include "lang/FullForm.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"


/**
 * A conjunction form is a multiset of clause forms
 * 
 * TODO: this should determine order of clauses from multiset iteration order
 */
Datum CreateFullForm(Datum const * clauseForms, size8 nClauseForms, index8 const * order)
{
	TypedAtom uniqueClauseForms[nClauseForms];
	for(index8 i = 0; i < nClauseForms; i++) {
		index8 j = order ? order[i] : i;
		uniqueClauseForms[i] = CreateTypedAtom(DT_ID, clauseForms[j]);
	}
	// reduce to unique roles
	uint32 multiplicities[nClauseForms];
	size8 nUniqueClauseForms = ReduceTypedAtomsArray(uniqueClauseForms, multiplicities, nClauseForms);

	// create a multiset of clause forms
	Datum conjunctionForm = CreateMultisetFromArrays(uniqueClauseForms, multiplicities, nUniqueClauseForms);
	TypedAtom conjunctionFormAtom = CreateTypedAtom(DT_ID, conjunctionForm);
	AssertFact(GetCorePredicateForm(FORM_CONJUNCTION_FORM), &conjunctionFormAtom);
	return conjunctionForm;
}


bool IsConjunctionForm(Datum atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_CONJUNCTION_FORM),
		GetCoreRoleName(ROLE_CONJUNCTION_FORM)
	);
}


size8 FullFormNUniqueClauseForms(Datum form)
{
	return MultisetNUniqueElements(form);
}


size8 FullFormNClauseFormsTotal(Datum form)
{
	return MultisetSize(form);
}


size8 FullFormArity(Datum form)
{
	// the arity of a cojunction is the sum of unique terms arity * multiple
	MultisetIterator iterator;
	MultisetIterate(form, &iterator);
	size8 arity = 0;
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		uint8 clauseArity = ClauseArity(elementMultiple.element.datum);
		arity += clauseArity * elementMultiple.multiple;
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	return arity;
}

/**
 * Traverse and print a form to stdout
 */
void PrintFullForm(Datum form)
{
	MultisetIterator iterator;
	MultisetIterate(form, &iterator);

	PrintChar('(');
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			PrintClauseForm(elementMultiple.element.datum);
			PrintCString(" & ");
		}
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	PrintChar(')');
}

