
#include "kernel/UInt.h"
#include "lang/ClauseForm.h"
#include "lang/TermForm.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/lookup.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"


Atom CreateClauseForm(Atom const * termForms, size8 nTermForms)
{
	// reduce to unique terms
	// here we need an array of atoms, since they will be stored in a multiset
	TypedAtom uniqueTermForms[nTermForms];
	for(index8 i = 0; i < nTermForms; i++)
		uniqueTermForms[i] = (TypedAtom) {.type = AT_ID, .atom = termForms[i]};
	uint32 multiplicities[nTermForms];
	size8 nUniqueTermForms = ReduceTypedAtomsArray(uniqueTermForms, multiplicities, nTermForms);

	IFactDraft draft;
	IFactBegin(&draft);

	AddMultisetToIFactFromArrays(&draft, uniqueTermForms, multiplicities, nUniqueTermForms);

	IFactBeginConjunction(
		&draft,
		GetCorePredicateForm(FORM_CLAUSE_FORM),
		RegistryGetCoreTable(FORM_CLAUSE_FORM),
		0
	);
	IFactAddClause(&draft, &invalidAtom);
	IFactEndConjunction(&draft);	

	return IFactEnd(&draft);
}


bool IsClauseForm(Atom form)
{
	return AtomHasRole(
		form,
		GetCorePredicateForm(FORM_CLAUSE_FORM),
		GetCoreRoleName(ROLE_CLAUSE_FORM)
	);
}


size8 ClauseNUniqueTerms(Atom clauseForm)
{
	return MultisetNUniqueElements(clauseForm);
}


size8 ClauseNTermsTotal(Atom clauseForm)
{
	return MultisetSize(clauseForm);
}


size8 ClauseArity(Atom clauseForm)
{
	// the arity of a clause is the sum of unique terms arity * multiple
	MultisetIterator iterator;
	MultisetIterate(clauseForm, &iterator);
	size8 arity = 0;
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		uint8 termArity = TermFormArity(elementMultiple.element.atom);
		arity += termArity * elementMultiple.multiple;
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	return arity;
}


void PrintClauseForm(Atom clauseForm)
{	
	MultisetIterator iterator;
	MultisetIterate(clauseForm, &iterator);

	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			PrintTermForm(elementMultiple.element.atom);
			PrintCString(" | ");
		}
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
}

