/**
 * A clause form is a multiset of term forms
 */

#ifndef CLAUSEFORM_H
#define CLAUSEFORM_H

#include "TermForm.h"

/**
 * Create a clause form from a list of term forms, possibly containing duplicates.
 */
Datum CreateClauseForm(Datum const * termForms, size8 nTerms);

bool IsClauseForm(Datum form);

size8 ClauseNUniqueTerms(Datum clauseForm);
size8 ClauseNTermsTotal(Datum clauseForm);

size8 ClauseArity(Datum clauseForm);

/**
 * Print a clause form to stdout
 */
void PrintClauseForm(Datum clauseForm);


#endif	// CLAUSEFORM_H
