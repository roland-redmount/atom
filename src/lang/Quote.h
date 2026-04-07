/**
 * A quote is defined by the relation (quote @q quoted @atom)
 *
 * Only expressions (DT_IDs) can be quoted
 * Quoted atoms are (will be) used in reflection.
 * 
 * NOTE: For now, this mechanism is not used. We are so far only
 * using quoting for variables, and we currently encode quoting
 * within the DT_VARIABLE datum instead. * 
 */

#ifndef QUOTE_H
#define QUOTE_H

#include "lang/Atom.h"
#include "lang/Datum.h"


Datum CreateQuote(Datum quoted);

bool IsQuote(Datum atom);

Datum QuoteGetQuoted(Datum quote);

void PrintQuoted(Datum quote);


# endif	// QUOTE_H
