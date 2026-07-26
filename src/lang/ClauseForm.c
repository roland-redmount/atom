
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
	// here we need an array of typed atoms, since they will be stored in a multiset
	Atom uniqueTermForms[nTermForms];
	CopyMemory(termForms, uniqueTermForms, nTermForms * sizeof(Atom));
	uint32 multiplicities[nTermForms];
	size8 nUniqueTermForms = ReduceAtomsArray(uniqueTermForms, multiplicities, nTermForms);

	IFactDraft draft;
	IFactBegin(&draft);

	AddMultisetToIFactFromArrays(&draft, uniqueTermForms, multiplicities, nUniqueTermForms, AT_ID);

	// (clause-form @form)
	RelationTable const * clauseFormTable = FindRelationTable(
		GetCorePredicateForm(FORM_CLAUSE_FORM),
		1, (byte[]) {AT_ID}
	);	
	IFactBeginConjunction(&draft, clauseFormTable, 0);
	IFactAddTuple(&draft, (Atom[]) {(Atom) {0}});
	IFactEndConjunction(&draft);	

	return IFactEnd(&draft);
}


bool IsClauseForm(Atom form)
{
	return AtomHasRole(
		form,
		GetCoreRelationTable(RELATION_CLAUSE_FORM),
		GetCoreRoleName(ROLE_CLAUSE_FORM)
	);
}


size8 ClauseFormNTermForms(Atom clauseForm)
{
	return MultisetNUniqueElements(clauseForm, AT_ID);
}


size8 ClauseFormNTerms(Atom clauseForm)
{
	return MultisetSize(clauseForm, AT_ID);
}


size8 ClauseArity(Atom clauseForm)
{
	// the arity of a clause is the sum of unique terms arity * multiple
	MultisetIterator iterator;
	MultisetIterate(clauseForm, AT_ID, &iterator);
	size8 arity = 0;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		uint8 termArity = TermFormArity(elementMultiple.element);
		arity += termArity * elementMultiple.multiple;
	}
	MultisetIteratorEnd(&iterator);
	return arity;
}


void PrintClauseForm(Atom clauseForm)
{	
	MultisetIterator iterator;
	MultisetIterate(clauseForm, AT_ID, &iterator);

	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			PrintTermForm(elementMultiple.element);
			PrintCString(" | ");
		}
	}
	MultisetIteratorEnd(&iterator);
}

