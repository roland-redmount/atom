/**
 * A conjunction form, consisting of a multiset of term forms
 */

#ifndef CONJUNCTION_FORM_H
#define CONJUNCTION_FORM_H

#include "lang/TypedAtom.h"
#include "lang/TermForm.h"


Atom CreateConjunctionForm(Atom const termForms[], size8 nTermForms);
void ReleaseConjunctionForm(Atom form);

bool IsConjunctionForm(Atom form);

size8 ConjunctionFormNUniqueTermForms(Atom form);
size8 ConjunctionFormNTermsTotal(Atom form);
size8 ConjunctionFormArity(Atom form);

void PrintConjunctionForm(Atom form);


#endif	// CONJUNCTION_FORM_H
