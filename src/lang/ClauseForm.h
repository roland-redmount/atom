/**
 * A clause form is a multiset of term forms
 */

#ifndef CLAUSEFORM_H
#define CLAUSEFORM_H

#include "TermForm.h"

/**
 * Create a clause form from a list of term forms, possibly containing duplicates.
 */
Atom CreateClauseForm(Atom const * termForms, size8 nTerms);

bool IsClauseForm(Atom form);

size8 ClauseNUniqueTerms(Atom clauseForm);
size8 ClauseNTermsTotal(Atom clauseForm);

size8 ClauseArity(Atom clauseForm);

/**
 * Print a clause form to stdout
 */
void PrintClauseForm(Atom clauseForm);


#endif	// CLAUSEFORM_H
