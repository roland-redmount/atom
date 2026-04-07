/**
 * A B-tree implementation of a relation table.
 * 
 * TODO: this currently relies on Josh Baker's btree.c implementation
 * https://github.com/tidwall/btree.c
 * This implementation is rather complex and hard to understand, and I'm
 * probably not using it efficiently. The RelationBTreeRemoveTuples()
 * function in particular is inefficient as I'm not sure how to implement
 * it in terms of btree.c internal functions.
 * Since we need to search based on variables, we're using a compare() function
 * that is not a complete ordering, and I don't know if that might cause
 * problems as some point.
 * 
 * For these reasons, at some point we should write our own B-tree implementation.
 */

#include "datumtypes/Variable.h"
#include "btree/btree.h"
#include "kernel/ifact.h"
#include "kernel/RelationBTree.h"
#include "kernel/tuples.h"
#include "lang/TypedAtom.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "util/ResizingArray.h"


// we store this in the btree->udata field
typedef union s_BTreeUserData {
	struct {
		uint8 nColumns;
		uint8 reserved[7];
	} fields;
	data64 value;
} __attribute__((packed)) BTreeUserData;


/**
 * Comparison function c(t, q) used to compare a tuple to a query.
 * If the query tuple contains variables, any columns after the leftmost variable
 * are ignored, so that any tuple matching all leftmost non-variables of the query
 * compares equal to it.
 * If the query tuple contains no variables, this function is the same as
 * CompareTuples().
 * 
 * NOTE: compareQuery() returning 0 does NOT guarantee that the tuple matches
 * the query in the sense of TupleMatch().
 * For example, with the set of tuples ordered by CompareTuples()
 * 
 *  (1, 2, 1)
 *  (1, 2, 3)
 *  (3, 4, 1)
 *  (3, 4, 5)
 *  (4, 2, 0)
 * 
 * and query = (3, _, 1), we obtain
 * 
 *  (1, 2, 1) < query
 *  (1, 2, 3) < query
 *  (3, 4, 1) = query
 *  (3, 4, 5) = query
 *  (4, 2, 0) > query
 * 
 * Of the two tuples that compare equal to query, only (3, 4, 1) matches query
 * according TupleMatch().
 * compareQuery() and CompareTuples() are compatible in the sense that 
 * q <= t1 and t1 <= t2 implies q <= t2
 * 
 * TODO: Currently, atom ordering within tuples is dictated by the ordering
 * of roles in the relation's form, which may not be the optimal order
 * for indexing. For example, for the form (position list element) the
 * "indexing" column order should be (list position element) to achieve
 * efficient searcher for quey tuples (@list _ _) and (@list @position _).
 * We therefore need to store tuples in a different atom order than that
 * given by the form.
 */

static int8 compareQuery(TypedAtom const * tuple, TypedAtom const * query, size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++) {
		TypedAtom queryAtom = query[i];
		if(queryAtom.type == DT_VARIABLE) {
			if(VariableIsQuoted(queryAtom)) {
				// If the query variable is quoted, we remove the (outermost) quote.
				// This allows querying for a variable _x stored in a relation (foo)
				// using (foo '_x')
				// TODO: review the semantics of this!
				queryAtom = UnquoteVariable(queryAtom);
			}
			else {
				// Unquoted query variable matches any atom
				break;
			}
		}
		else {
			// all other atom types	
			int atomOrdering = CompareTypedAtoms(tuple[i], queryAtom);
			if(atomOrdering < 0)
				return -1;
			if(atomOrdering > 0)
				return 1;
		}
	}
	return 0;
}


static int8 btreeCompareItems(void const * item, void const * queryItem, size32 itemSize)
{
	size8 nColumns = itemSize / sizeof(TypedAtom);
	return compareQuery((TypedAtom const *) item, (TypedAtom const *) queryItem, nColumns);
}


BTree * CreateRelationBTree(size8 nColumns)
{
	// we create a btree where one item = one tuple
	return BTreeCreate(
		sizeof(TypedAtom) * nColumns,  // item size
		btreeCompareItems,
		0  // free()
	);
}


void FreeRelationBTree(BTree * tree)
{
	BTreeFree(tree);
}


size8 RelationBTreeNColumns(BTree const * tree)
{
	return tree->itemSize / sizeof(TypedAtom);
}


size32 RelationBTreeNRows(BTree const * tree)
{
	return BTreeNItems(tree);
}


static void advanceIterator(RelationBTreeIterator * iterator)
{
	// advance iterator to next matching tuple
	if(iterator->queryTuple) {
		while(BTreeIteratorHasItem(&(iterator->treeIterator))) {
			TypedAtom const * tuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
			if(TupleMatch(tuple, iterator->queryTuple, iterator->nColumns))
				break;
			BTreeIteratorNext(&(iterator->treeIterator));
		}
		// else no matching item in btree
	}
	// else start from the first tuple
}

void RelationBTreeIterate(BTree * tree, TypedAtom const * queryTuple, RelationBTreeIterator * iterator)
{
	SetMemory(iterator, sizeof(RelationBTreeIterator), 0);
	iterator->btree = tree;
	iterator->nColumns = RelationBTreeNColumns(tree);
	iterator->queryTuple = queryTuple;
	BTreeIterate(&(iterator->treeIterator), tree, queryTuple, 0);
	advanceIterator(iterator);
}


bool RelationBTreeIteratorHasTuple(RelationBTreeIterator const * iterator)
{
	return BTreeIteratorHasItem(&(iterator->treeIterator));
}


void RelationBTreeIteratorNext(RelationBTreeIterator * iterator)
{
	BTreeIteratorNext(&(iterator->treeIterator));
	advanceIterator(iterator);
}


/**
 * Accessor functions. These are needed to clear the ATOM_PROTECTED flag
 * in the case where tuples are part of a defining fact
 */
TypedAtom RelationBTreeIteratorGetAtom(RelationBTreeIterator const * iterator, index8 i)
{
	ASSERT(i < iterator->nColumns);
	TypedAtom const * tuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
	TypedAtom atom = tuple[i];
	atom.flags &= (~ATOM_PROTECTED);
	return atom;
}


void RelationBTreeIteratorGetTuple(RelationBTreeIterator const * iterator, TypedAtom * tuple)
{
	TypedAtom const * btreeTuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
	for(index8 i = 0; i < iterator->nColumns; i++) {
		TypedAtom atom = btreeTuple[i];
		atom.flags &= (~ATOM_PROTECTED);
		tuple[i] = atom;
	}
}


void RelationBTreeIteratorEnd(RelationBTreeIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->treeIterator));
	SetMemory(iterator, sizeof(RelationBTreeIterator), 0);
}


void RelationBTreeQuerySingle(BTree * tree, TypedAtom const * queryTuple, TypedAtom * resultTuple)
 {
 	RelationBTreeIterator iterator;
 	RelationBTreeIterate(tree, queryTuple, &iterator);
 	ASSERT(RelationBTreeIteratorHasTuple(&iterator));
 	RelationBTreeIteratorGetTuple(&iterator, resultTuple);
 	// verify the relation has a single tuple only
 	RelationBTreeIteratorNext(&iterator);
 	ASSERT(!RelationBTreeIteratorHasTuple(&iterator));
 	RelationBTreeIteratorEnd(&iterator);
 }


byte RelationBTreeAddTuple(BTree * tree, TypedAtom const * tuple)
{
	if(!IFactCheckTuple(tree, tuple))
		return TUPLE_PROTECTED;

	size8 nColumns = RelationBTreeNColumns(tree);
	if(BTreeInsert(tree, tuple) == BTREE_INSERTED) {
		// tuple was added, acquire atoms
		for(index8 i = 0; i < nColumns; i++) {
			if(!(tuple[i].flags & ATOM_PROTECTED))
				AcquireTypedAtom(tuple[i]);
		}
		return TUPLE_ADDED;
	}
	else
		return TUPLE_EXISTS;
	
}


/**
 * A tuple is protected if any of its atoms is protected.
 */
static bool tupleisProtected(TypedAtom const * tuple, size8 nAtoms)
{
	for(index8 i = 0; i < nAtoms; i++) {
		if(tuple[i].flags & ATOM_PROTECTED)
			return true;
	}
	return false;
}


size32 RelationBTreeRemoveTuples(BTree * tree, TypedAtom const * queryTuple, uint8 mode)
{
	ASSERT(!BTreeIsWriteLocked(tree));
	size8 nColumns = RelationBTreeNColumns(tree);

	// retrieve all matching tuples
	ResizingArray tuples;
	CreateResizingArray(&tuples, sizeof(TypedAtom) * nColumns, 10);

	RelationBTreeIterator iterator;
	size32 nTuplesToDelete = 0;
	RelationBTreeIterate(tree, queryTuple, &iterator);
	while(RelationBTreeIteratorHasTuple(&iterator)) {
		// access the current tuple directly to see the ATOM_PROTECTED flag
		TypedAtom const * tuple = BTreeIteratorPeekItem(&(iterator.treeIterator));
		if((mode == REMOVE_PROTECTED) || !tupleisProtected(tuple, nColumns)) {
			ResizingArrayAppend(&tuples, tuple);
			nTuplesToDelete++;
		}
		RelationBTreeIteratorNext(&iterator);
	}
	RelationBTreeIteratorEnd(&iterator);

	// Release all atoms references by tuples.
	// NOTE: this cannot be done while iterating over the tree,
	// as IFactRelease() calls RelationBTreeRemoveTuples()
	TypedAtom * tupleAtoms = ResizingArrayGetMemory(&tuples);
	for(index32 i = 0; i < nTuplesToDelete * nColumns; i++) {
		// protected atoms can only be released by calling IFactRelease()
		if(!(tupleAtoms[i].flags & ATOM_PROTECTED))
			ReleaseTypedAtom(tupleAtoms[i]);
	}

	// delete tuples
	for(index32 i = 0; i < nTuplesToDelete; i++)
		ASSERT(BTreeDelete(tree, &tupleAtoms[i * nColumns]) == BTREE_DELETED);

	FreeResizingArray(&tuples);
	return nTuplesToDelete;
}


// for debugging
void RelationBTreeDump(BTree * tree)
{
	size8 nColumns = RelationBTreeNColumns(tree);
	PrintF("BTree %u columns\n", nColumns);

	RelationBTreeIterator iterator;
	RelationBTreeIterate(tree, 0, &iterator);
	TypedAtom tuple[nColumns];
	size32 nTuples = 0;
	while(RelationBTreeIteratorHasTuple(&iterator)) {
		RelationBTreeIteratorGetTuple(&iterator, tuple);
		PrintChar('{');
		// TODO: we should probably not print the full representaiton
		// of identified atoms, as it triggers repeated queries
		PrintTuple(tuple, iterator.nColumns);
		PrintChar('}');
		PrintChar('\n');
		RelationBTreeIteratorNext(&iterator);
		nTuples++;
	}
	RelationBTreeIteratorEnd(&iterator);
	PrintF("%u tuples\n", nTuples);
}
