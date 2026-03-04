/**
 * A quote is defined by the relation (quote @q quoted @atom)
 *
 * Only expressions (DT_VARIABLE, DT_FORMULA  ?) can be quoted
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


Atom CreateQuote(Atom quoted);

void QuoteSetTuple(Atom * tuple, Atom quote, Atom quoted);

bool IsQuote(Atom atom);

Atom QuoteGetQuoted(Atom quote);

void PrintQuoted(Atom quote);


# endif	// QUOTE_H
