#ifndef PAIR_H
#define PAIR_H


#include "kernel/ifact.h"


/**
 * Create the (pair left right) relation and its service. PairShutdown() removes them.
 */
void PairSetup(void);

void PairShutdown(void);

/**
 * Create a pair atom, defined by the left and right elements,
 * both AT_ID atoms.
 * 
 * TODO: this uses (pair @atom left @left right @right) as the
 * identifying fact; we should probably have two facts
 * (pair @atom left @left) & (pair @atom right @right) instead.
 * This is not a true trinary relation.
 */
Atom CreatePair(Atom left, Atom right);

/**
 * Add pair ifacts (pair @atom left @left right @right) to a draft IFact
 */
void AddPairToIFact(IFactDraft * draft, Atom left, Atom right);

/**
 * Test if an AT_ID atom is a pair
 */
bool IsPair(Atom atom);

/**
 * Return either the left or right element from a pair (an AT_ID atom),
 * depending on element = PAIR_LEFT or element = PAIR_RIGHT.
 */
Atom PairGetElement(Atom pair, uint8 element);

#define PAIR_LEFT	1
#define PAIR_RIGHT	2


void PrintPair(Atom pair);


#endif	// PAIR_H

