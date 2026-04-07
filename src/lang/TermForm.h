/**
 * A term form specifies one predicate form with sign (negated or not)
 * 
 * (term-form t predicate-form p sign s)
 */

#include "lang/PredicateForm.h"


Datum CreateTermForm(Datum predicateForm, bool sign);

bool IsTermForm(Datum form);

Datum GetPredicateForm(Datum termForm);
bool TermFormGetSign(Datum termForm);

void PrintTermForm(Datum termForm);

size8 TermFormArity(Datum termForm);
