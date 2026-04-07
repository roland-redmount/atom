/**
 * Implementation of a relation table using the btree data structure.
 */

#ifndef RELATION_B_TREE_H
#define RELATION_B_TREE_H

#include "btree/btree.h"
#include "lang/TypedAtom.h"


typedef struct s_RelationBTreeIterator {
	BTree * btree;
	BTreeIterator treeIterator;
	TypedAtom const * queryTuple;	// NOTE: this is a pointer since the tuple is variable size
	size8 nColumns;
} RelationBTreeIterator;


BTree * CreateRelationBTree(size8 nColumns);
void FreeRelationBTree(BTree * tree);

size8 RelationBTreeNColumns(BTree const * table);
size32 RelationBTreeNRows(BTree const * tree);


/**
 * Initialize an iterator returning all tuples matching queryTuple
 * according to TupleMatch();  or, if queryTuple is 0, returning all tuples in the B-tree.
 * After this call, the iterator will be positioned at the first tuple
 * matching the query, if any, and RelationBTreeIteratorHasTuple() may be called.
 * The tree is write-locked to prevent modification while iterating.
 * 
 * NOTE: the iterator does not keep a copy of queryTuple,
 * so it must remain unchanged during the iteration.
 */
void RelationBTreeIterate(BTree * tree, TypedAtom const * queryTuple, RelationBTreeIterator * iterator);

/**
 * Test if the iterator has a next element.
 * If this function returns true, the tuple can be accessed by 
 * RelationBTreeIteratorGetTuple() or RelationBTreeIteratorGetAtom().
 */
bool RelationBTreeIteratorHasTuple(RelationBTreeIterator const * iterator);

/**
 * Advance the iterator to the next tuple matching the query, if any
 */
void RelationBTreeIteratorNext(RelationBTreeIterator * iterator);

TypedAtom RelationBTreeIteratorGetAtom(RelationBTreeIterator const * iterator, index8 i);

/**
 * Copy the iterator's current tuple into a tuple provided by the caller.
 */
void RelationBTreeIteratorGetTuple(RelationBTreeIterator const * iterator, TypedAtom * tuple);

/**
 * Terminate the iterator, releasing lock from the tree.
 */
void RelationBTreeIteratorEnd(RelationBTreeIterator * iterator);

/**
 * Query the relation and return a single tuple.
 * The relation table must have exactly one tuple matching the query.
 */
void RelationBTreeQuerySingle(BTree * tree, TypedAtom const * queryTuple, TypedAtom * resultTuple);


/**
 * Add a aingle tuple to the relation, acquiring each atom in the tuple.
 * Does not add entries to lookup; see AssertFact()
 */
byte RelationBTreeAddTuple(BTree * tree, TypedAtom const * tuple);

// result codes for RelationBTreeAddTuple()
#define TUPLE_ADDED			1
#define TUPLE_EXISTS		2
#define TUPLE_PROTECTED		3	// adding would violate an ifact definition


/**
 * Remove tuples from the BTree matching the query. To remove all tuples, set
 * queryTuple to 0.
 * 
 * If mode is REMOVE_NORMAL, tuples containing an atom with the ATOM_PROTECTED bit set
 * will NOT be removed. If mode is REMOVE_PROTECTED, all matching tuples are removed.
 * Releases a reference to each DT_ID atom in a removed tuple, except for atoms with
 * the ATOM_PROTECTED bit set.
 */
size32 RelationBTreeRemoveTuples(BTree * tree, TypedAtom const * queryTuple, uint8 mode);

#define REMOVE_NORMAL		0
#define REMOVE_PROTECTED	1


/**
 * Print out an entire relation table, for debugging
 */
void RelationBTreeDump(BTree * tree);

#endif	// RELATION_B_TREE_H
