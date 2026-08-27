/**
 * A multiset is an unordered collection where a given element can occur multiple times.
 * This is a core relation since it is required to represent forms.
 */

#ifndef MULTISET_H
#define MULTISET_H

#include "kernel/ifact.h"
#include "kernel/operator.h"


typedef struct s_ElementMultiple {
	Atom element;
	size32 multiple;
} ElementMultiple;


typedef ElementMultiple (*MultisetElementGenerator)(index32 index, void const * data);

/**
 * Create an immutable multiset (AT_ID) using an element generator function
 */
Atom CreateMultiset(
	MultisetElementGenerator generator, void const * data, size32 nUniqueElements, byte elementType);

void AddMultisetToIFact(
	IFactDraft * draft,
	MultisetElementGenerator generator, void const * data, size32 nUniqueElements, byte elementType);


/**
 * Create a multiset from arrays of atoms (unique elements) and corresponding multiples.
 * The array order is not significant.
 */
Atom CreateMultisetFromArrays(
	Atom const atoms[], size32 const multiples[], size32 nUniqueElements, byte elementType);

void AddMultisetToIFactFromArrays(
	IFactDraft * draft, Atom const atoms[], size32 const multiples[], size32 nUniqueElements, byte elementType);

/**
 * Evaluate (multiset @atom)
 */
bool IsMultiset(Atom atom);

/**
 * Number of unique elements in the multiset, not including multiples
 */
size32 MultisetNUniqueElements(Atom multiset, byte elementType);

/**
 * Total number of elements in the multiset, including multiples
 */
size32 MultisetSize(Atom multiset, byte elementType);

size32 MultisetGetElementMultiple(Atom multiset, Atom element);

/**
 * Find an order for a given array of elements consistent with
 * the iteration order obtained by MultisetIterate(), up to multiples.
 * For example, if the multi set iterates as {a b b c d d} and the elements array
 * is (c b d b d a), possible ordering are (0-based)
 * 
 *  (5, 1, 3, 0, 2, 4)
 *  (5, 3, 1, 0, 2, 4)
 *  (5, 1, 3, 0, 4, 2)
 *  (5, 3, 1, 0, 4, 2)
 *  
 * So the atom elements[order[i]] matches the multiset element at iteration order i.
 * This ordering determines the order of atoms in formulas, and the order
 * of columns in relation tables.
 * 
 * NOTE: the length of elements and order arrays must equal MultisetSize()
 */
void MultisetIterationOrder(Atom multiset, byte elementType, Atom const elements[], index8 order[], size8 nElements);

/**
 * Iteration over a multiset.
 * 
 * Although the multiset has no "semantic" ordering, this iterator
 * is guaranteed to produce tuples according to the order defined by
 * TypedTupleCompare(), generating a stable ordering. This is used to
 * align the roles of a form with an ordered actor list by a formula.
 */

typedef struct s_MultisetIterator
{
	Atom queryTuple[3];
	OperatorContext * context;
} MultisetIterator;


void MultisetIterate(Atom multiset, byte elementType, MultisetIterator * iterator);

bool MultisetIteratorNext(MultisetIterator * iterator);

ElementMultiple MultisetIteratorGetElement(MultisetIterator const * iterator);

void MultisetIteratorEnd(MultisetIterator * iterator);

/**
 * Find the element at position k in a multiset, according to the iteration order.
 * This uses MultisetIterator to reach position k.
 */
Atom MultisetFindElement(Atom multiset, byte elementType, index32 k);


void PrintMultiset(Atom multiset);


#endif	// MULTISET_H
