/**
 * Implementation of a relation table storing tuples in a B-tree data structure.
 * This implements both a MachineServiceProvider (queries for various relations)
 * and a mechanism for asserting facts
 */

#ifndef RELATION_B_TREE_H
#define RELATION_B_TREE_H

#include "btree/btree.h"
#include "kernel/RelationTable.h"
#include "kernel/tuple.h"
#include "kernel/service.h"


// TODO: replace this with a service provider registry ...
extern MachineServiceProvider bTreeServiceProvider;

extern RelationTableProvider btreeTableProvider;

/*
typedef struct s_RelationBTree {
	BTree * btree;
	// NOTE: these are now store inh RelationTable,
	// so we really just need a BTree * 
	// size8 nColumns;
	// byte atomTypes[];
} RelationBTree;
*/

/**
 * Register relation table provider
 */
void RelationBTreeInitialize(void);

/**
 * Create a RelationBTree.
 * NOTE: use CreateRelationTable() instead
 */
// RelationBTree * CreateRelationBTree(size8 nColumns, byte const * atomTypes);

// NOTE: this should be handled automatically by RelationTable
// void FreeRelationBTree(RelationBTree * tree);

// size8 RelationBTreeNColumns(RelationBTree const * table);
// size32 RelationBTreeNRows(RelationBTree const * tree);

// NOTE: iterating over a B-tree should now be done by calling the appropriate service.
// The service signature determines the input arguments. The first n arguments
// are inputs, and the remainder outputs; this is due to B-tree lexiographic ordering.
// TODO: this should be handled by a service call

typedef struct s_RelationBTreeIterator {
	RelationTable * table;
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
	RelationTable * table, Atom const * queryTuple, size8 nInputs, RelationBTreeIterator * iterator);

/**
 * Advance the iterator to the next tuple matching the query, if any.
 * Returns true if an tuple was found, corresponding to a B-Tree service
 * yielding a fact.
 */
bool RelationBTreeIteratorNext(RelationBTreeIterator * iterator);

/**
 * Returns true if RelationBTreeIteratorNext() has not been called.
 */
bool RelationBTreeIteratorBeforeFirst(RelationBTreeIterator * iterator);

/**
 * Get the atom at 0-based index i in the current tuple.
 */
Atom RelationBTreeIteratorGetAtom(RelationBTreeIterator const * iterator, index8 i);

/**
 * Copy the iterator's current tuple into a tuple provided by the caller.
 */
void RelationBTreeIteratorGetTuple(RelationBTreeIterator const * iterator, Atom * tuple);

/**
 * View the iterator's current tuple
 */
Atom const * RelationBTreeIteratorPeekTuple(RelationBTreeIterator const * iterator);

/**
 * Terminate the iterator, releasing lock from the tree.
 */
void RelationBTreeIteratorEnd(RelationBTreeIterator * iterator);

/**
 * Query a B-tree relation table and return a single tuple.
 * The relation table must have exactly one tuple matching the query.
 */
void RelationBTreeQuerySingle(RelationTable * table, Atom const * queryTuple, size8 nInputs, Atom * resultTuple);

/**
 * Query the relation and return a single TypedAtom from a single tuple.
 * The relation table must have exactly one tuple matching the query.
 */
Atom RelationBTreeQuerySingleAtom(RelationTable * table, Atom const * queryTuple, size8 nInputs, index8 index);


/**
 * Add a single tuple to the relation, acquiring each atom in the tuple.
 * Does not add entries to lookup; see AssertFact().
 * For ifacts, the identified parameter is a 1-based position of the identified atom;
 * else set identified = 0
 * 
 * NOTE: Should this really be acquiring atoms? Or move that to AssertFact() ?
 */
// byte RelationBTreeAddTuple(RelationBTree * tree, Atom const * tuple, uint8 identified);



/**
 * Remove tuples from the BTree matching the query.
 * If protected is nonzero, tuples with a protected atom in this position will be removed,
 * otherwise, only tuples without identified atoms will be removed.
 * Releases a reference to each AT_ID atom in a removed tuple, except identified atoms.
 * To remove all tuples, set queryTuple to 0 (the nuclear option).
 */
size32 RelationBTreeRemoveTuples(RelationTable * table, Atom const * queryTuple, size8 nInputs, uint8 identified);

// #define REMOVE_NORMAL		0
// #define REMOVE_PROTECTED	1


/**
 * Print out an entire relation table, for debugging
 */
void RelationBTreeDump(RelationTable * table);

#endif	// RELATION_B_TREE_H
