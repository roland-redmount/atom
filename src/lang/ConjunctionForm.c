
#include "lang/ConjunctionForm.h"
#include "lang/TermForm.h"
#include "lang/TermMultiset.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"


Atom CreateConjunctionForm(Atom const termForms[], size8 nTermForms)
{
	return CreateTermMultisetForm(termForms, nTermForms, RELATION_CONJUNCTION_FORM);
}


bool IsConjunctionForm(Atom form)
{
	return IsTermMultisetForm(form, RELATION_CONJUNCTION_FORM, ROLE_CONJUNCTION_FORM);
}


size8 ConjunctionFormNUniqueTermForms(Atom form)
{
	return TermMultisetNUniqueTermForms(form);
}


size8 ConjunctionFormNTermsTotal(Atom form)
{
	return TermMultisetNTerms(form);
}


size8 ConjunctionFormArity(Atom form)
{
	return TermMultisetArity(form);
}


/**
 * Traverse and print a form to stdout
 */
void PrintConjunctionForm(Atom form)
{
	MultisetIterator iterator;
	MultisetIterate(form, AT_ID, &iterator);

	PrintChar('(');
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			PrintTermForm(elementMultiple.element);
			PrintCString(" & ");
		}
	}
	MultisetIteratorEnd(&iterator);
	PrintChar(')');
}
