/**
 * A conjunction form, consisting of a multiset of term forms
 */

#ifndef CONJUNCTION_FORM_H
#define CONJUNCTION_FORM_H

#include "lang/TypedAtom.h"
#include "lang/TermForm.h"

/**
 * Create a conjunction form from an array of term forms.
 * Duplicate term forms yield multiplicity > 1 in the conjunction form.
 */
Atom CreateConjunctionForm(Atom const termForms[], size8 nTermForms);
void ReleaseConjunctionForm(Atom form);

bool IsConjunctionForm(Atom form);

/**
 * Return the number of unique term forms in the conjunction form
 */
size8 ConjunctionFormNUniqueTermForms(Atom form);

/**
 * Return the total number of term forms, accounting for multiples
 */
size8 ConjunctionFormNTermsTotal(Atom form);

size8 ConjunctionFormArity(Atom form);

/**
 * Write the term forms of the conjunction form to the termForms[]
 * array, including duplicate forms when multiplicity > 1.
 * The termForms[] array must accomodate at least ConjunctionFormNTermsTotal()
 * elements. Returns the number of term forms written.
 */
size8 ConjunctionFormGetTermForms(Atom form, Atom termForms[]);

void PrintConjunctionForm(Atom form);


#endif	// CONJUNCTION_FORM_H
