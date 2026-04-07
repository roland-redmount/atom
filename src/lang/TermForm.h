/**
 * A term form specifies one predicate form with sign (negated or not)
 * 
 * (term-form t predicate-form p sign s)
 */

#include "lang/PredicateForm.h"


Atom CreateTermForm(Atom predicateForm, bool sign);

bool IsTermForm(Atom form);

Atom GetPredicateForm(Atom termForm);
bool TermFormGetSign(Atom termForm);

void PrintTermForm(Atom termForm);

size8 TermFormArity(Atom termForm);
