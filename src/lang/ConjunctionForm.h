/**
 * A conjunction form, consisting of a multiset of clause forms
 */

#ifndef CONJUNCTION_FORM_H
#define CONJUNCTION_FORM_H

#include "lang/TypedAtom.h"
#include "lang/ClauseForm.h"


Atom CreateConjunctionForm(Atom const clauseForms[], size8 nClauseForms);
void ReleaseConjunctionForm(Atom form);

bool IsConjunctionForm(Atom form);

size8 ConjunctionFormNUniqueClauseForms(Atom form);
size8 ConjunctionFormNClauseFormsTotal(Atom form);
size8 ConjunctionFormArity(Atom form);

// uint8 ClauseMultiplicity(Atom form, uint8 index);
// Atom GetClauseForm(Atom form, index8 index);

void PrintConjunctionForm(Atom form);


#endif	// CONJUNCTION_FORM_H
