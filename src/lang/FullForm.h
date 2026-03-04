/**
 * A conjunction form, here called "full form" for brevity and to avoid
 * confusion with generic form atoms, which can be either predicate, clause or full forms
 */

#ifndef FULLFORM_H
#define FULLFORM_H

#include "Atom.h"
#include "ClauseForm.h"


Atom CreateFullForm(Atom const * clauseForms, size8 nClauseForms, index8 const * order);
void ReleaseFullForm(Atom form);

bool IsConjunctionForm(Atom form);

size8 FullFormNUniqueClauseForms(Atom form);
size8 FullFormNClauseFormsTotal(Atom form);
size8 FullFormArity(Atom form);

// uint8 ClauseMultiplicity(Atom form, uint8 index);
// Atom GetClauseForm(Atom form, index8 index);

void PrintForm(Atom form);


#endif	// FULLFORM_H
