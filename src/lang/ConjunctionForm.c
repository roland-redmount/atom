
#include "lang/ConjunctionForm.h"
#include "lang/TermForm.h"
#include "lang/TermMultiset.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"


Atom CreateConjunctionForm(Atom const termForms[], size8 nTermForms)
{
	return CreateTermMultiset(termForms, nTermForms, RELATION_CONJUNCTION_FORM);
}


bool IsConjunctionForm(Atom form)
{
	Operator * op = GetCoreOperator(SERVICE_CONJUNCTION_FORM);
	Atom arguments[1] = {form};
	return OperatorCallOnce(op, arguments);
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


// NOTE: this could go to TermMultiSet instead
size8 ConjunctionFormGetTermForms(Atom form, Atom termForms[])
{
	size8 nTerms = 0;
	MultisetIterator iterator;
	MultisetIterate(form, AT_ID, &iterator);
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 i = 0; i < elementMultiple.multiple; i++)
			termForms[nTerms++] = elementMultiple.element;
	}
	MultisetIteratorEnd(&iterator);
	return nTerms;
}


/**
 * Traverse and print a form to stdout
 */
void PrintConjunctionForm(Atom form)
{
	MultisetIterator iterator;
	MultisetIterate(form, AT_ID, &iterator);

	PrintChar('(');
	bool isFirst = true;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			if(!isFirst)
				PrintCString(" & ");
			PrintTermForm(elementMultiple.element);
			isFirst = false;
		}
	}
	MultisetIteratorEnd(&iterator);
	PrintChar(')');
}
