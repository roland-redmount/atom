
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
Atom CreateFullForm(Atom const * clauseForms, size8 nClauseForms, index8 const * order)
{
	TypedAtom uniqueClauseForms[nClauseForms];
	for(index8 i = 0; i < nClauseForms; i++) {
		index8 j = order ? order[i] : i;
		uniqueClauseForms[i] = CreateTypedAtom(AT_ID, clauseForms[j]);
	}
	// reduce to unique roles
	uint32 multiplicities[nClauseForms];
	size8 nUniqueClauseForms = ReduceTypedAtomsArray(uniqueClauseForms, multiplicities, nClauseForms);

	// create a multiset of clause forms
	Atom conjunctionForm = CreateMultisetFromArrays(uniqueClauseForms, multiplicities, nUniqueClauseForms);
	TypedAtom conjunctionFormAtom = CreateTypedAtom(AT_ID, conjunctionForm);
	AssertFact(GetCorePredicateForm(FORM_CONJUNCTION_FORM), &conjunctionFormAtom);
	return conjunctionForm;
}


bool IsConjunctionForm(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_CONJUNCTION_FORM),
		GetCoreRoleName(ROLE_CONJUNCTION_FORM)
	);
}


size8 FullFormNUniqueClauseForms(Atom form)
{
	return MultisetNUniqueElements(form);
}


size8 FullFormNClauseFormsTotal(Atom form)
{
	return MultisetSize(form);
}


size8 FullFormArity(Atom form)
{
	// the arity of a cojunction is the sum of unique terms arity * multiple
	MultisetIterator iterator;
	MultisetIterate(form, &iterator);
	size8 arity = 0;
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		uint8 clauseArity = ClauseArity(elementMultiple.element.atom);
		arity += clauseArity * elementMultiple.multiple;
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	return arity;
}

/**
 * Traverse and print a form to stdout
 */
void PrintFullForm(Atom form)
{
	MultisetIterator iterator;
	MultisetIterate(form, &iterator);

	PrintChar('(');
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			PrintClauseForm(elementMultiple.element.atom);
			PrintCString(" & ");
		}
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	PrintChar(')');
}

