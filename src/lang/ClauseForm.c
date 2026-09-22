
#include "lang/ClauseForm.h"
#include "lang/TermForm.h"
#include "lang/TermMultiset.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"


Atom CreateClauseForm(Atom const termForms[], size8 nTermForms)
{
	return CreateTermMultisetForm(termForms, nTermForms, RELATION_CLAUSE_FORM);
}


bool IsClauseForm(Atom form)
{
	return IsTermMultisetForm(form, RELATION_CLAUSE_FORM, ROLE_CLAUSE_FORM);
}


size8 ClauseFormNTermForms(Atom clauseForm)
{
	return TermMultisetNUniqueTermForms(clauseForm);
}


size8 ClauseFormNTerms(Atom clauseForm)
{
	return TermMultisetNTerms(clauseForm);
}


size8 ClauseArity(Atom clauseForm)
{
	return TermMultisetArity(clauseForm);
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
