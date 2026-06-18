/**
 * Implementation of a relation table storing tuples in a B-tree data structure.
 * This implements both a MachineServiceProvider (queries for various relations)
 * and a mechanism for asserting facts
 */

#ifndef RELATION_B_TREE_H
#define RELATION_B_TREE_H

#include "btree/btree.h"
#include "kernel/tuple.h"
#include "kernel/service.h"


// TODO: replace this with a service provider registry ...
extern MachineServiceProvider bTreeServiceProvider;

typedef struct s_RelationBTree {
	BTree * btree;
	size8 nColumns;
	byte atomTypes[];
} RelationBTree;

/**
 * Create a RelationBTree.
 * TODO: for a typed version, this needs the column types.
 * We then create services with the corresponding signatures.
 */
RelationBTree * CreateRelationBTree(size8 nColumns, byte const * atomTypes);

void FreeRelationBTree(RelationBTree * tree);

// size8 RelationBTreeNColumns(RelationBTree const * table);
size32 RelationBTreeNRows(RelationBTree const * tree);

// NOTE: iterating over a B-tree should now be done by calling the appropriate service.
// The service signature determines the input arguments. The first n arguments
// are inputs, and the remainder outputs; this is due to B-tree lexiographic ordering.

typedef struct s_RelationBTreeIterator {
	RelationBTree * tree;
	BTreeIterator treeIterator;
	struct s_BTreeTuple * queryTuple;
} RelationBTreeIterator;


/**
 * Initialize an iterator returning all tuples matching queryTuple, or,
 * if queryTuple is 0, returning all tuples in the B-tree.
 * The iterator will be positioned before the first item, and 
 * RelationBTreeIteratorNext() must be called before RelationBTreeIteratorHasTuple().
 * The tree is write-locked to prevent modification while iterating.
 */
void RelationBTreeIterate(
	RelationBTree * tree, Tuple const * queryTuple, size8 nInputs, RelationBTreeIterator * iterator);

/**
 * Advance the iterator to the next tuple matching the query, if any.
 * Returns true if an tuple was found, corresponding to a B-Tree service
 * yielding a fact.
 */
bool RelationBTreeIteratorNext(RelationBTreeIterator * iterator);

/**
 * Returns true if RelationBTreeIteratorNext() has not been called.
 */
bool BTreeIteratorBeforeFirst(BTreeIterator * iterator);

/**
 * Get the atom at 0-based index i in the current tuple.
 */
Atom RelationBTreeIteratorGetAtom(RelationBTreeIterator const * iterator, index8 i);

/**
 * Copy the iterator's current tuple into a tuple provided by the caller.
 */
void RelationBTreeIteratorGetTuple(RelationBTreeIterator const * iterator, Tuple * tuple);

/**
 * View the iterator's current tuple
 */
Atom const * RelationBTreeIteratorPeekTuple(RelationBTreeIterator const * iterator);

/**
 * Terminate the iterator, releasing lock from the tree.
 */
void RelationBTreeIteratorEnd(RelationBTreeIterator * iterator);

/**
 * Query the relation and return a single tuple.
 * The relation table must have exactly one tuple matching the query.
 */
void RelationBTreeQuerySingle(BTree * tree, Tuple const * queryTuple, Tuple * resultTuple);

/**
 * Query the relation and return a single TypedAtom from a single tuple.
 * The relation table must have exactly one tuple matching the query.
 */
TypedAtom RelationBTreeQuerySingleAtom(BTree * tree, Tuple const * queryTuple, index8 index);


/**
 * Add a single tuple to the relation, acquiring each atom in the tuple.
 * Does not add entries to lookup; see AssertFact().
 * To mark a protected atom (for ifacts), set protected to a 1-based position.
 * 
 * NOTE: Should this really be acquiring atoms? Or move that to AssertFact() ?
 */
byte RelationBTreeAddTuple(RelationBTree * tree, Atom const * tuple, uint8 protected);

// result codes for RelationBTreeAddTuple()
#define TUPLE_ADDED			1
#define TUPLE_EXISTS		2
// #define TUPLE_PROTECTED		3	// adding would violate an ifact definition


/**
 * Remove tuples from the BTree matching the query.
 * If protected is nonzero, tuples with a protected atom in this position will be removed,
 * otherwise, only tuples without identified atoms will be removed.
 * Releases a reference to each AT_ID atom in a removed tuple, except identified atoms.
 * To remove all tuples, set queryTuple to 0 (the nuclear option).
 */
size32 RelationBTreeRemoveTuples(BTree * tree, Tuple const * queryTuple, uint8 identified);

// #define REMOVE_NORMAL		0
// #define REMOVE_PROTECTED	1


/**
 * Print out an entire relation table, for debugging
 */
void RelationBTreeDump(BTree * tree);

#endif	// RELATION_B_TREE_H
