/**
 * Enumerating combinations of k out of n items. A combination is an array of
 * k ascending indices in 0 .. n-1, and combinations are enumerated in lexicographic order.
 */
#ifndef COMBINATIONS_H
#define COMBINATIONS_H

#include "platform.h"


/**
 * Set the indices array to the first combination of k items, indices = 0, 1, ..., k-1.
 */
void FirstCombination(size8 k, index8 indices[]);

/**
 * Advance the indices array to the next combination of k out of n items.
 * Returns false, leaving the indices array unchanged, when the indices array holds the last
 * combination, which is n-k, ..., n-1.
 */
bool NextCombination(size8 n, size8 k, index8 indices[]);


#endif	// COMBINATIONS_H
