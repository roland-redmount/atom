/**
 * A list stores a sequence of n elements with positions 1, 2, ..., n.
 * 
 * NOTE: this was a "core" class when a formula was implemented a pair
 * of a form and and a list of actors. It was then untyped and stored
 * arbitrary elements. The new version is typed, and so there may be
 * multiple relation tables, one for each element type (esssentially
 * a typed array). This could be removed from the kernel.
 */

#ifndef LIST_H
#define	LIST_H

#include "lang/TypedAtom.h"
#include "kernel/ifact.h"
#include "kernel/service.h"


/**
 * Create a list from a callback function generating list element atoms.
 * The ListElementGenerator will be called with a 0-based index into the list.
 */
typedef Atom (*ListElementGenerator)(index32 index, void const * data);

Atom CreateList(ListElementGenerator generator, void const * data, byte elementType, size32 nElements);

/**
 * Create a list from an array of typed atoms
 */
Atom CreateListFromArray(Atom const * listElements, byte elementType, size8 nAtoms);

// Atom CreateListFromTuple(TypedTuple const * tuple);

/**
 * Add list ifacts obtained from the generator to an exising IFact draft.
 * TODO: this now assumes the element type is AT_LETTER. We need to take element type
 * as a parameter and use the corresponding (typed) relation table.
 */
void AddListToIFact(
	IFactDraft * draft, ListElementGenerator generator, void const * data, byte elementType, size32 nElements);


/**
 * Begin a draft list, for stepwise construction.
 */
void ListBegin(IFactDraft * draft);

/**
 * Append one element to a draft list.
 * Returns the (1-based) position of the new element,
 * which is the same as the new length of the list.
 */
// index32 ListAddElement(IFactDraft * draft, Atom element);

/**
 * Finalize a draft list, returning the completed list atom.
 */
Atom ListEnd(IFactDraft * draft);

/**
 * An atom @a "is a list" if there exists a fact (list @a length _).
 * The empty list satisfies (list @emptyList length 0) but does
 * not have any (list positin element) facts.
 */
bool IsList(Atom atom);


/**
 * Return l from the query (list @list length l)
 */
size32 ListLength(Atom list);

/**
 * Return e from the query (list @list position @position element e)
 * with the specified element type. Position is 1-based.
 */
Atom ListGetElement(Atom list, index32 position);

/**
 * Copy all list elements into a given array
 * (assumed to be large enough to hold the eleements)
 */
// void ListGetElementsArray(Atom list, Atom * elements);

/**
 * Set the elements of tuple according to the (list position element) form.
 */
// void ListSetTuple(Atom * tuple, Atom list, Atom position, Atom element);

/**
 * Return the first position p from the query
 * (list @list position p element @element)
 * or 0 if the element is not in the list.
 */
index32 ListGetPosition(Atom list, Atom element);

/**
 * Copy the elements of a list to a Tuple.
 * The Tuple must have the same number of elements.
 * 
 * NOTE: this is no longer well defined when the list element type is constant
 */
// void CopyListToTuple(Atom list, TypedTuple * tuple);

void PrintList(Atom list);

int8 ListLexicalOrdering(Atom list1, Atom list2, int8 (*compare)(Atom, Atom));


typedef struct s_ListIterator
{
	Atom queryTuple[3];
	ServiceContext * context;
} ListIterator;


/**
 * Create a list iterator. This is a thin wrapper around RelationBTreeIterator.
 */
void ListIterate(Atom list, ListIterator * iterator);

bool ListIteratorNext(ListIterator * iterator);

Atom ListIteratorGetElement(ListIterator const * iterator);

void ListIteratorEnd(ListIterator * iterator);


#endif  // LIST_H
