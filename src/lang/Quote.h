/**
 * A quote is defined by the relation (quote @q quoted @atom)
 *
 * Only expressions (DT_IDs) can be quoted
 * Quoted atoms are (will be) used in reflection.
 * 
 * NOTE: For now, this mechanism is not used. We are so far only
 * using quoting for variables, and we currently encode quoting
 * within the DT_VARIABLE atom instead. * 
 */

#ifndef QUOTE_H
#define QUOTE_H

#include "lang/TypedAtom.h"
#include "lang/Atom.h"


Atom CreateQuote(Atom quoted);

bool IsQuote(Atom atom);

Atom QuoteGetQuoted(Atom quote);

void PrintQuoted(Atom quote);


# endif	// QUOTE_H
