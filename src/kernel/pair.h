#ifndef PAIR_H
#define PAIR_H

#include "lang/Atom.h"
#include "kernel/ifact.h"


#define PAIR_LEFT	1
#define PAIR_RIGHT	2

/**
 * Create a pair atom, defined by the left and right elements
 * 
 * TODO: this uses (pair @atom left @left right @right) as the
 * identifying fact; we should probably have two facts
 * (pair @atom left @left) & (pair @atom right @right) instead.
 * This is not a true trinary relation.
 */
Datum CreatePair(Atom left, Atom right);

/**
 * Add pair ifacts (pair @atom left @left right @right) to a draft IFact
 */
void AddPairToIFact(IFactDraft * draft, Atom left, Atom right);


bool IsPair(Datum atom);

Atom PairGetElement(Datum pair, uint8 element);

void PrintPair(Datum pair);


#endif	// PAIR_H

