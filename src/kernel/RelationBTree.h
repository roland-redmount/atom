/**
 * Implementation of a RelationTable storing tuples in a B-tree data structure,
 * with operators for searching on the leading columns.
 * 
 * NOTE: in the future this should be a "plugin" module, should probably move
 * to a separate folder.
 */

#ifndef RELATION_B_TREE_H
#define RELATION_B_TREE_H

#include "btree/btree.h"
#include "kernel/RelationTable.h"
#include "kernel/tuple.h"
#include "kernel/operator.h"


// TODO: replace this with a machine provider registry ...
extern MachineProvider bTreeProvider;
extern RelationTableProvider btreeTableProvider;

// NOTE: "RelationBTree" sounds more like a B-tree of relations than a relation
// backed by a B-tree ... rename to BTreeRelation ?
typedef struct s_RelationBTree {
	BTree * btree;
	index8 * indexColumns;
	size8 nColumns;
	// byte * atomTypes;
} RelationBTree;

/**
 * Create the B-tree storage of a relation table. This function is called by
 * btreeTableProvider.createStorage().
 *
 * The arity and index column order are copied here rather than read off the RelationTable
 * on every call, so that a RelationBTree stays usable as a data structure on its own.
 *
 * NOTE: the B-tree relation does not store the column types.
 */
RelationBTree * CreateRelationBTree(size8 nColumns, byte const atomTypes[], index8 const indexColumns[]);

void FreeRelationBTree(RelationBTree * relation);

/**
 * Return number of tuples in a B-tree relation.
 */
size32 RelationBTreeNRows(RelationBTree const * relation);

/**
 * Add a tuple to a B-tree relation.
 */
byte RelationBTreeAddTuple(RelationBTree * relation, Atom const tuple[], uint8 idPosition);

// TODO: this should be registered with a relation table provider registry


// B-tree iterator structure.
// The service signature determines the input arguments. The first n arguments
// are inputs, and the remainder outputs; this is due to B-tree lexiographic ordering.

typedef struct s_RelationBTreeIterator {
	RelationBTree * relation;
	struct s_BTreeTuple * queryTuple;
	BTreeIterator treeIterator;
} RelationBTreeIterator;


/**
 * Initialize an iterator returning all tuples matching queryTuple, or,
 * if queryTuple is 0, returning all tuples in the B-tree.
 * The iterator will be positioned before the first item, and 
 * RelationBTreeIteratorNext() must be called before RelationBTreeIteratorHasTuple().
 * The tree is write-locked to prevent modification while iterating.
 * 
 * NOTE: iterating should usually be done by calling the appropriate operator.
 */
void RelationBTreeIterate(
	RelationBTree * relation, Atom const queryTuple[], size8 nInputs, RelationBTreeIterator * iterator);

/**
 * Advance the iterator to the next tuple matching the query, if any.
 * Returns true if an tuple was found, corresponding to a B-Tree operator
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
void RelationBTreeIteratorGetTuple(RelationBTreeIterator const * iterator, Atom tuple[]);

/**
 * View the iterator's current tuple
 * NOTE: this is no longer feasible as RelationBTree reorders columns internally
 */
// Atom const * RelationBTreeIteratorPeekTuple(RelationBTreeIterator const * iterator);

/**
 * Terminate the iterator, releasing lock from the tree.
 */
void RelationBTreeIteratorEnd(RelationBTreeIterator * iterator);

/**
 * Query a B-tree relation table and return a single tuple.
 * The relation table must have exactly one tuple matching the query.
 */
// void RelationBTreeQuerySingle(RelationTable * table, Atom const ueryTuple[], size8 nInputs, Atom resultTuple[]);

/**
 * Query the relation and return a single TypedAtom from a single tuple.
 * The relation table must have exactly one tuple matching the query.
 */
// Atom RelationBTreeQuerySingleAtom(RelationTable * table, Atom const queryTuple[], size8 nInputs, index8 index);


/**
 * Remove a tuple from the BTree matching the query. See RelationTableRemoveTuple()
 */
byte RelationBTreeRemoveTuple(RelationBTree * relation, Atom const tuple[], uint8 idPosition);

// size32 RelationBTreeRemoveTuples(BTree * btree, Atom const queryTuple[], size8 nInputs, uint8 identified);

/**
 * High-level method to give the relation of the given signature B-tree storage, creating
 * and registering the relation if it does not exist yet. The form is a term form.
 * The returned table holds the creation reference; see DropRelationTable().
 */
RelationTable * CreateRelationBTreeWithServices(
	Atom termForm, size8 nColumns, byte const atomTypes[], index8 const indexColumns[]);

/**
 * Bootstrapping variant taking the predicate form directly.
 * See CreateRelationBootstrap()
 */
RelationTable * CreateRelationBTreeWithServicesBootstrap(
	Atom termForm, Atom predicateForm, size8 nColumns, byte const atomTypes[], index8 const indexColumns[]);


#endif	// RELATION_B_TREE_H
