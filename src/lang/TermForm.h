/**
 * A term form specifies one predicate form with sign (negated or not)
 * 
 * (term-form t predicate-form p sign s)
 */

#include "lang/PredicateForm.h"


/**
 * Create a term form from a predicate form and a sign.
 * The sign is false if the term is negated.
 */
Atom CreateTermForm(Atom predicateForm, bool sign);

bool IsTermForm(Atom form);

/**
 * Return the predicate form contained in the term form
 */
Atom TermFormGetPredicateForm(Atom termForm);

/**
 * Return false is the term is negated
 */
bool TermFormGetSign(Atom termForm);

/**
 * Return the "opposite" of the given term form, flipping the sign.
 * The caller obtains a reference to the returned form.
 */
Atom TermFormCreateOppositeForm(Atom termForm);

/**
 * Print a term form
 */
void PrintTermForm(Atom termForm);

/**
 * Number of actors in a term of this form.
 */
size8 TermFormArity(Atom termForm);
