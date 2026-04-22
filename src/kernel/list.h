/**
 * A list (DT_LIST) stores a sequence of n elements
 * with positions 1, 2, ..., n
 */


#ifndef LIST_H
#define	LIST_H

#include "lang/TypedAtom.h"
#include "kernel/ifact.h"
#include "kernel/RelationBTree.h"


/**
 * Create a list from a callback function generating list element atoms.
 */
typedef TypedAtom (*ListElementGenerator)(index32 index, void const * data);

Atom CreateList(ListElementGenerator generator, void const * data, size32 nElements);

/**
 * Create a list from an array of list elements
 */
Atom CreateListFromArray(TypedAtom const * listElements, size8 nAtoms);


/**
 * Add list ifacts obtained from the generator to an exising IFact draft.
 */
void AddListToIFact(IFactDraft * draft, ListElementGenerator generator, void const * data, size32 nElements);


/**
 * Begin a draft list, for stepwise construction.
 */
void ListBegin(IFactDraft * draft);

/**
 * Append one element to a draft list.
 * Returns the (1-based) position of the new element,
 * which is the same as the new length of the list.
 */
index32 ListAddElement(IFactDraft * draft, TypedAtom element);

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
 * Assign values to a tuple from the (list length) relation
 */
void ListLengthSetTuple(Tuple * tuple, TypedAtom list, TypedAtom length);


/**
 * Return l from the query (list @list length l)
 */
size32 ListLength(Atom list);

/**
 * Return e from the query (list @list position @position element e)
 * 
 * NOTE: position is 1-based.
 */
TypedAtom ListGetElement(Atom list, index32 position);

/**
 * Copy all list elements into a given array
 * (assumed to be large enough to hold the eleements)
 */
void ListGetElementsArray(Atom list, TypedAtom * elements);

/**
 * Set the elements of tuple according to the (list position element) form.
 */
void ListSetTuple(Tuple * tuple, TypedAtom list, TypedAtom position, TypedAtom element);

/**
 * Return the first position p from the query
 * (list @list position p element @element)
 * or 0 if the element is not in the list.
 * 
 * NOTE: since this relies on a quey (list @list position _ element @element),
 * we cannot use it to find specific variables in a list, such as
 * (list @list position_ element _x)
 * This will always return position 1 since _x matches any element.
 * To find elements that are variables, we would have to use a quoted variable '_x.
 * This is not yet implemented.
 */
index32 ListGetPosition(Atom list, TypedAtom element);

void PrintList(Atom list);

int8 ListLexicalOrdering(Atom list1, Atom list2, int8 (*compare)(TypedAtom, TypedAtom));


/**
 * Iteration over a list.
 * align the roles of a form with an ordered actor list by DT_FORMULA.
 */

typedef struct s_ListIterator
{
	Tuple * queryTuple;
	RelationBTreeIterator treeIterator;
} ListIterator;


void ListIterate(Atom list, ListIterator * iterator);
bool ListIteratorHasNext(ListIterator const * iterator);
TypedAtom ListIteratorGetElement(ListIterator const * iterator);
void ListIteratorNext(ListIterator * iterator);
void ListIteratorEnd(ListIterator * iterator);

#endif  // LIST_H
