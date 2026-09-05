/**
 * A list stores a sequence of n elements with positions 1, 2, ..., n.
 * 
 * NOTE: this was a "core" class when a formula was implemented a pair
 * of a form and and a list of actors. It was then untyped and stored
 * arbitrary elements. The new version is typed, and so there may be
 * multiple relation tables, one for each element type (esssentially
 * a typed array).
 */

#ifndef LIST_H
#define	LIST_H

#include "lang/TypedAtom.h"
#include "kernel/ifact.h"
#include "kernel/operator.h"
#include "kernel/Relation.h"
#include "kernel/RelationTable.h"


/**
 * Create the list relations and their services. ListShutdown() removes them.
 */
void ListSetup(void);

void ListShutdown(void);


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


/**
 * Roles of the (list position element) predicate, naming a column
 * of the relation; see GetListRoleIndex()
 */
#define LIST_ROLE_LIST			0
#define LIST_ROLE_POSITION		1
#define LIST_ROLE_ELEMENT		2

/**
 * Roles of the (list length) predicate, naming a column
 * of the relation; see GetListLengthRoleIndex()
 */
#define LIST_LENGTH_ROLE_LIST	0
#define LIST_LENGTH_ROLE_LENGTH	1

/**
 * The role name "list", an AT_NAME atom.
 */
Atom GetListRoleName(void);

Atom GetListPredicateForm(void);
Atom GetListTermForm(void);

Atom GetListLengthPredicateForm(void);
Atom GetListLengthTermForm(void);

/**
 * The column index of each role of (list position element), indexed by LIST_ROLE_*.
 * The array has 3 entries.
 */
index8 const * GetListRoleIndex(void);

/**
 * Set a tuple of the (list position element) relation in canonical column order,
 * given a tuple in the role order (list position element).
 */
void ListSetTuple(Atom const inputTuple[], Atom tuple[]);

/**
 * Set a byte array in the canonical column order of (list position element),
 * given an array in the role order (list position element).
 */
void ListSetByteArray(byte const inputArray[], byte array[]);

/**
 * The column index of each role of (list length), indexed by LIST_LENGTH_ROLE_*.
 * The array has 2 entries.
 */
index8 const * GetListLengthRoleIndex(void);

/**
 * The (list position element) relation storing elements of the given type,
 * which is AT_ID or AT_LETTER.
 */
Relation GetListRelation(byte elementType);

RelationTable * GetListRelationTable(byte elementType);

/**
 * The service (list <ID position >INT element >elementType) of the relation
 * returned by GetListRelation()
 */
Operator * GetListOperator(byte elementType);

Relation GetListLengthRelation(void);

RelationTable * GetListLengthRelationTable(void);

/**
 * The service (list <ID length >INT)
 */
Operator * GetListLengthOperator(void);


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
