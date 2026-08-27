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
#include "kernel/operator.h"


/**
 * Create a list from a callback function generating list element atoms.
 * The ListElementGenerator will be called with a 0-based index into the list.
 */
typedef Atom (*ListElementGenerator)(index32 index, void const * data);

Atom CreateList(ListElementGenerator generator, void const * data, byte elementType, size32 nElements);

/**
 * Create a list from an array of typed atoms
 */
Atom CreateListFromArray(Atom const listElements[], byte elementType, size8 nAtoms);

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
 * Return the first position p from the query
 * (list @list position p element @element)
 * or 0 if the element is not in the list.
 */
index32 ListGetPosition(Atom list, Atom element);

/**
 * Print a list atom
 */
void PrintList(Atom list);

/**
 * Determine the lexical ordering of two lists.
 * 
 * NOTE: it is currently not possible to use this in for canonical ordering of list
 * (and string) atoms, since this function depends on B-tree iteration,
 * which leads to infinite recursion when comparing B-tree ḱeys.
 */
int8 ListLexicalOrdering(Atom list1, Atom list2, int8 (*compare)(Atom, Atom));


typedef struct s_ListIterator
{
	Atom queryTuple[3];
	OperatorContext * context;
} ListIterator;


/**
 * Create a list iterator. This is a thin wrapper around RelationBTreeIterator.
 */
void ListIterate(Atom list, ListIterator * iterator);

bool ListIteratorNext(ListIterator * iterator);

Atom ListIteratorGetElement(ListIterator const * iterator);

void ListIteratorEnd(ListIterator * iterator);


#endif  // LIST_H
