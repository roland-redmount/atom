
#include "lang/ConjunctionForm.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"


/**
 * A conjunction form is a multiset of clause forms
 */
Atom CreateConjunctionForm(Atom const * clauseForms, size8 nClauseForms)
{
	TypedAtom uniqueClauseForms[nClauseForms];
	for(index8 i = 0; i < nClauseForms; i++) 
		uniqueClauseForms[i] = CreateTypedAtom(AT_ID, clauseForms[i]);
	// reduce to unique roles
	uint32 multiplicities[nClauseForms];
	size8 nUniqueClauseForms = ReduceTypedAtomsArray(uniqueClauseForms, multiplicities, nClauseForms);

	IFactDraft draft;
	IFactBegin(&draft);

	AddMultisetToIFactFromArrays(&draft, uniqueClauseForms, multiplicities, nUniqueClauseForms);

	// (clause-form @form)
	IFactBeginConjunction(
		&draft,
		GetCorePredicateForm(FORM_CONJUNCTION_FORM),
		RegistryGetCoreBTreeService(FORM_CONJUNCTION_FORM),
		0
	);
	TypedTuple * tuple = CreateTypedTuple(1);
	IFactAddClause(&draft, tuple);
	FreeTypedTuple(tuple);
	IFactEndConjunction(&draft);	

	return IFactEnd(&draft);
}


bool IsConjunctionForm(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_CONJUNCTION_FORM),
		GetCoreRoleName(ROLE_CONJUNCTION_FORM)
	);
}


size8 ConjunctionFormNUniqueClauseForms(Atom form)
{
	return MultisetNUniqueElements(form);
}


size8 ConjunctionFormNClauseFormsTotal(Atom form)
{
	return MultisetSize(form);
}


size8 ConjunctionFormArity(Atom form)
{
	// the arity of a cojunction is the sum of unique terms arity * multiple
	MultisetIterator iterator;
	MultisetIterate(form, &iterator);
	size8 arity = 0;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		uint8 clauseArity = ClauseArity(elementMultiple.element.atom);
		arity += clauseArity * elementMultiple.multiple;
	}
	MultisetIteratorEnd(&iterator);
	return arity;
}

/**
 * Traverse and print a form to stdout
 */
void PrintConjunctionForm(Atom form)
{
	MultisetIterator iterator;
	MultisetIterate(form, &iterator);

	PrintChar('(');
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			PrintClauseForm(elementMultiple.element.atom);
			PrintCString(" & ");
		}
	}
	MultisetIteratorEnd(&iterator);
	PrintChar(')');
}

