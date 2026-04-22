/**
 * A relation table based on a B-tree. Relies on btree.c for implemention.
 */

#include "btree/btree.h"
#include "kernel/ifact.h"
#include "kernel/RelationBTree.h"
#include "kernel/tuple.h"
#include "lang/TypedAtom.h"
#include "lang/Variable.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "util/ResizingArray.h"


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

static int8 compareQuery(Tuple const * tuple, Tuple const * query)
{
	for(index8 i = 0; i < tuple->nAtoms; i++) {
		TypedAtom queryAtom = TupleGetElement(query, i);
		if(queryAtom.type == AT_VARIABLE) {
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
			TypedAtom tupleAtom = TupleGetElement(tuple, i);
			int atomOrdering = CompareTypedAtoms(tupleAtom, queryAtom);
			if(atomOrdering < 0)
				return -1;
			if(atomOrdering > 0)
				return 1;
			// else contiue
		}
	}
	return 0;
}


static int8 btreeCompareItems(void const * item, void const * queryItem, size32 itemSize)
{
	return compareQuery((Tuple const *) item, (Tuple const *) queryItem);
}


BTree * CreateRelationBTree(size8 nColumns)
{
	// we create a btree where one item = one tuple
	return BTreeCreate(
		TupleNBytes(nColumns),  // item size
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
	return TupleNAtoms(tree->itemSize);
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
			Tuple const * tuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
			if(TupleMatch(tuple, iterator->queryTuple))
				break;
			BTreeIteratorNext(&(iterator->treeIterator));
		}
		// else no matching item in btree
	}
	// else start from the first tuple
}


void RelationBTreeIterate(BTree * tree, Tuple const * queryTuple, RelationBTreeIterator * iterator)
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
 * Accessor functions.
 * 
 * NOTE: these are no longer strictly necessary as ATOM_PROTECTED is no longer used
 */
TypedAtom RelationBTreeIteratorGetAtom(RelationBTreeIterator const * iterator, index8 i)
{
	ASSERT(i < iterator->nColumns);
	Tuple const * tuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
	return TupleGetElement(tuple, i);
}


void RelationBTreeIteratorGetTuple(RelationBTreeIterator const * iterator, Tuple * tuple)
{
	Tuple const * btreeTuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
	CopyTuples(btreeTuple, tuple);
}


Tuple const * RelationBTreeIteratorPeekTuple(RelationBTreeIterator const * iterator)
{
	return BTreeIteratorPeekItem(&(iterator->treeIterator));
}


void RelationBTreeIteratorEnd(RelationBTreeIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->treeIterator));
	SetMemory(iterator, sizeof(RelationBTreeIterator), 0);
}


void RelationBTreeQuerySingle(BTree * tree, Tuple const * queryTuple, Tuple * resultTuple)
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


TypedAtom RelationBTreeQuerySingleAtom(BTree * tree, Tuple const * queryTuple, index8 index)
{
 	RelationBTreeIterator iterator;
 	RelationBTreeIterate(tree, queryTuple, &iterator);
 	ASSERT(RelationBTreeIteratorHasTuple(&iterator));
 	TypedAtom atom = RelationBTreeIteratorGetAtom(&iterator, index);
 	// verify the relation has a single tuple only
 	RelationBTreeIteratorNext(&iterator);
 	ASSERT(!RelationBTreeIteratorHasTuple(&iterator));
 	RelationBTreeIteratorEnd(&iterator);
	return atom;
 }


 /**
  * To add a protected atom (for ifact) simply set the protectedAtom field of the tuple
  */

byte RelationBTreeAddTuple(BTree * tree, Tuple const * tuple)
{
	ASSERT(RelationBTreeNColumns(tree) == tuple->nAtoms)
	
	if(!IFactCheckTuple(tree, tuple))
		return TUPLE_PROTECTED;

	if(BTreeInsert(tree, tuple) == BTREE_INSERTED) {
		// tuple was added, acquire atoms
		for(index8 i = 0; i < tuple->nAtoms; i++) {
			if((i + 1) != tuple->protectedAtom)
				AcquireTypedAtom(TupleGetElement(tuple, i));
		}
		return TUPLE_ADDED;
	}
	else
		return TUPLE_EXISTS;
	
}

size32 RelationBTreeRemoveTuples(BTree * tree, Tuple const * queryTuple, uint8 mode)
{
	ASSERT(!BTreeIsWriteLocked(tree));
	size8 nColumns = RelationBTreeNColumns(tree);
	if(queryTuple) {
		ASSERT(nColumns == queryTuple->nAtoms)
	}

	// retrieve all matching tuples
	ResizingArray tuplesArray;
	CreateResizingArray(&tuplesArray, TupleNBytes(nColumns), 10);

	RelationBTreeIterator iterator;
	size32 nTuplesToDelete = 0;
	RelationBTreeIterate(tree, queryTuple, &iterator);
	while(RelationBTreeIteratorHasTuple(&iterator)) {
		Tuple const * tuple = BTreeIteratorPeekItem(&(iterator.treeIterator));
		if((mode == REMOVE_PROTECTED) || !TupleIsProtected(tuple)) {
			ResizingArrayAppend(&tuplesArray, tuple);
			nTuplesToDelete++;
		}
		RelationBTreeIteratorNext(&iterator);
	}
	RelationBTreeIteratorEnd(&iterator);

	// Release all atoms references by tuples.
	// NOTE: this cannot be done while iterating over the tree,
	// as IFactRelease() calls RelationBTreeRemoveTuples()
	for(index32 i = 0; i < nTuplesToDelete; i++) {
		Tuple * tuple = ResizingArrayGetElement(&tuplesArray, i);
		for(index32 j = 0; j < nColumns; j++) {
			// protected atoms can only be released by calling IFactRelease()
			if((j+1) != tuple->protectedAtom) {
				TypedAtom atom = TupleGetElement(tuple, j);
				ReleaseTypedAtom(atom);
			}
		}
	}
	// delete tuples
	for(index32 i = 0; i < nTuplesToDelete; i++) {
		Tuple * tuple = ResizingArrayGetElement(&tuplesArray, i);
		ASSERT(BTreeDelete(tree, tuple) == BTREE_DELETED);
	}

	FreeResizingArray(&tuplesArray);
	return nTuplesToDelete;
}


// for debugging
void RelationBTreeDump(BTree * tree)
{
	size8 nColumns = RelationBTreeNColumns(tree);
	PrintF("BTree %u columns\n", nColumns);

	RelationBTreeIterator iterator;
	RelationBTreeIterate(tree, 0, &iterator);
	size32 nTuples = 0;
	while(RelationBTreeIteratorHasTuple(&iterator)) {
		Tuple const * tuple = RelationBTreeIteratorPeekTuple(&iterator);
		// TODO: we should probably not print the full representaiton
		// of identified atoms, as it triggers repeated queries
		PrintTuple(tuple);
		PrintChar('\n');
		RelationBTreeIteratorNext(&iterator);
		nTuples++;
	}
	RelationBTreeIteratorEnd(&iterator);
	PrintF("%u tuples\n", nTuples);
}
