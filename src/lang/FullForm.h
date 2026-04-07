/**
 * A conjunction form, here called "full form" for brevity and to avoid
 * confusion with generic form atoms, which can be either predicate, clause or full forms
 */

#ifndef FULLFORM_H
#define FULLFORM_H

#include "lang/TypedAtom.h"
#include "lang/ClauseForm.h"


Datum CreateFullForm(Datum const * clauseForms, size8 nClauseForms, index8 const * order);
void ReleaseFullForm(Datum form);

bool IsConjunctionForm(Datum form);

size8 FullFormNUniqueClauseForms(Datum form);
size8 FullFormNClauseFormsTotal(Datum form);
size8 FullFormArity(Datum form);

// uint8 ClauseMultiplicity(Atom form, uint8 index);
// Atom GetClauseForm(Atom form, index8 index);

void PrintForm(Datum form);


#endif	// FULLFORM_H
