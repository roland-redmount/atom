/**
 * A relation table based on a B-tree. Relies on btree.c for implemention.
 * Provides both services (query) and agents (assert)
 */

#include "btree/btree.h"
#include "kernel/service.h"
#include "kernel/ifact.h"
#include "kernel/RelationBTree.h"
#include "kernel/typedtuple.h"
#include "lang/TypedAtom.h"
#include "lang/Variable.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "util/ResizingArray.h"


typedef struct s_BTreeTuple {
	size8 nAtoms;
	uint8 identified;
	// Only atoms[0] .. atoms[nAtomsPresent-1] are stored in the tuple.
	// This is = nAtoms in B-tree items (full tuple is stored),
	// but < nAtoms in queries as output arguments are not present.
	index8 nAtomsPresent;
	Atom atoms[];
} BTreeTuple;


static BTreeTuple * createBTreeTuple(size8 nAtoms, uint8 identified, index8 nAtomsPresent, Atom const * atoms)
{
	BTreeTuple * btreeTuple = Allocate(sizeof(BTreeTuple) + nAtoms * sizeof(Atom));
	btreeTuple->nAtoms = nAtoms;
	btreeTuple->identified = identified;
	btreeTuple->nAtomsPresent = nAtomsPresent;
	CopyMemory(atoms, btreeTuple->atoms, nAtoms * sizeof(Atom));
	return btreeTuple;
}


static void freeBTreeTuple(BTreeTuple * btreeTuple)
{
	Free(btreeTuple);
}


/**
 * Comparison function c(t, q) used to compare a tuple to a query.
 * If the query tuple contains variables, any columns after the leftmost variable
 * are ignored, so that any tuple matching all leftmost non-variables of the query
 * compares equal to it.
 * If the query tuple contains no variables, this function is the same as
 * TypedTupleCompare().
 * 
 * NOTE: compareQuery() returning 0 does NOT guarantee that the tuple matches
 * the query in the sense of TypedTupleMatch().
 * For example, with the set of tuples ordered by TypedTupleCompare()
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
 * according TypedTupleMatch().
 * compareQuery() and TypedTupleCompare() are compatible in the sense that 
 * q <= t1 and t1 <= t2 implies q <= t2
 * 
 * TODO: Currently, atom ordering within tuples is dictated by the ordering
 * of roles in the relation's form, which may not be the optimal order
 * for indexing. For example, for the form (position list element) the
 * "indexing" column order should be (list position element) to achieve
 * efficient searcher for quey tuples (@list _ _) and (@list @position _).
 * We therefore need to index tuples in a different atom order than that
 * given by the form.
 */

static int8 compareQuery(BTreeTuple const * tuple, BTreeTuple const * query, RelationBTree * tree)
{
	// compare the 
	for(index8 i = 0; i < query->nAtomsPresent; i++) {
		int atomOrdering = CompareAtoms(tuple->atoms[i], query->atoms[i]);
		if(atomOrdering < 0)
			return -1;
		if(atomOrdering > 0)
			return 1;
		// else continue
	}
	return 0;
}


static int8 btreeCompareItems(void const * item, void const * queryItem, void * data)
{
	return compareQuery((TypedTuple const *) item, (TypedTuple const *) queryItem, (RelationBTree *) data);
}


RelationBTree * CreateRelationBTree(size8 nColumns, byte const * atomTypes)
{
	RelationBTree * relationBTree = Allocate(sizeof(RelationBTree) + nColumns);
	relationBTree->nColumns = nColumns;
	CopyMemory(atomTypes, relationBTree->atomTypes, nColumns);

	// Each B-tree item stored
	size32 itemSize = sizeof(BTreeTuple) + nColumns * sizeof(Atom);
	relationBTree->btree = BTreeCreate(itemSize, btreeCompareItems, 0);
}


void FreeRelationBTree(RelationBTree * tree)
{
	BTreeFree(tree->btree);
	Free(tree);
}


// size8 RelationBTreeNColumns(RelationBTree const * tree)
// {
// 	return tree->nColumns;
// }


size32 RelationBTreeNRows(RelationBTree const * tree)
{
	return BTreeNItems(tree->btree);
}

/*
 * TODO: to support searching with variables when using an untyped Tuple,
 * we could set all variable atoms to zero and start iteration at the first item
 * that compares >= to the tuple; this requires modifying BTreeIteratorSeek()
 * Then we do linear search which doesn't require BTree to compare tuples.
 * A typed service would treat output parameters as variables.
 */

void RelationBTreeIterate(
	RelationBTree * tree, Atom const * queryTuple, size8 nInputs, RelationBTreeIterator * iterator)
{
	SetMemory(iterator, sizeof(RelationBTreeIterator), 0);
	iterator->tree = tree;
	if(queryTuple) {
		iterator->queryTuple = createBTreeTuple(tree->nColumns, 0, nInputs, queryTuple);
	}
	else
		iterator->queryTuple = 0;
	BTreeIterate(&(iterator->treeIterator), tree);
}


bool RelationBTreeIteratorNext(RelationBTreeIterator * iterator)
{
	if(!iterator->queryTuple)
		return BTreeIteratorNext(&(iterator->treeIterator));
	// else we have a query tuple
	bool foundItem;
	if(BTreeIteratorBeforeFirst(&iterator->treeIterator)) {
		// new iterator, seek to first match
		foundItem = BTreeIteratorSeek(&(iterator->treeIterator), iterator->queryTuple);
	}
	else
		foundItem = BTreeIteratorNext(&(iterator->treeIterator));
	if(!foundItem)
		return false;
	// advance iterator to next matching tuple
	do {
		Tuple const * tuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
		if(TypedTupleMatch(tuple, iterator->queryTuple))
			return true;
		;
	} while(BTreeIteratorNext(&(iterator->treeIterator)));
	// else no matching item in btree
	return false;
}


Atom RelationBTreeIteratorGetAtom(RelationBTreeIterator const * iterator, index8 i)
{
	ASSERT(i < iterator->tree->nColumns);
	BTreeTuple const * tuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
	return tuple->atoms[i];
}


void RelationBTreeIteratorGetTuple(RelationBTreeIterator const * iterator, Tuple * tuple)
{
	BTreeTuple const * btreeTuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
	ASSERT(tuple->nAtoms == btreeTuple->nAtoms)
	CopyMemory(btreeTuple->atoms, tuple->atoms, btreeTuple->nAtoms * sizeof(Atom));
}

// NOTE: not feasible to return at Tuple as the B-tree stores BTreeTuple
Atom const * RelationBTreeIteratorPeekTuple(RelationBTreeIterator const * iterator)
{
	BTreeTuple const * btreeTuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
	return btreeTuple->atoms;
}


void RelationBTreeIteratorEnd(RelationBTreeIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->treeIterator));
	if(iterator->queryTuple)
		FreeTypedTuple(iterator->queryTuple);
	SetMemory(iterator, sizeof(RelationBTreeIterator), 0);
}


void RelationBTreeQuerySingle(RelationBTree * tree, Atom const * queryTuple, TypedTuple * resultTuple)
 {
 	RelationBTreeIterator iterator;
 	RelationBTreeIterate(tree, queryTuple, &iterator);
 	ASSERT(RelationBTreeIteratorNext(&iterator));
 	RelationBTreeIteratorGetTuple(&iterator, resultTuple);
 	// verify the relation has a single tuple only
 	ASSERT(!RelationBTreeIteratorNext(&iterator));
 	RelationBTreeIteratorEnd(&iterator);
 }


Atom RelationBTreeQuerySingleAtom(RelationBTree * tree, Atom const * queryTuple, index8 index)
{
 	RelationBTreeIterator iterator;
 	RelationBTreeIterate(tree, queryTuple, &iterator);
 	ASSERT(RelationBTreeIteratorNext(&iterator));
 	Atom atom = RelationBTreeIteratorGetAtom(&iterator, index);
 	// verify the relation has a single tuple only
 	ASSERT(!RelationBTreeIteratorNext(&iterator));
 	RelationBTreeIteratorEnd(&iterator);
	return atom;
 }


byte RelationBTreeAddTuple(RelationBTree * tree, Atom const * tuple, uint8 identified)
{
	// if(!IFactCheckTuple(tree, tuple))
	// 	return TUPLE_PROTECTED;
	BTreeTuple * btreeTuple = createBTreeTuple(tree->nColumns, identified, tree->nColumns, tuple);
	if(BTreeInsert(tree, tuple) == BTREE_INSERTED) {
		// tuple was added, acquire atoms
		for(index8 i = 0; i < tree->nColumns; i++) {
			if(i + 1 != identified)
				AcquiredAtom(tuple[i], tree->atomTypes[i]);
		}
		return TUPLE_ADDED;
	}
	else
		return TUPLE_EXISTS;
	
}

size32 RelationBTreeRemoveTuples(RelationBTree * tree, Atom const * queryTuple, size8 nInputs, uint8 identified)
{
	ASSERT(!BTreeIsWriteLocked(tree->btree));

	// retrieve all matching tuples
	ResizingArray tuplesArray;
	CreateResizingArray(&tuplesArray, tree->nColumns * sizeof(Atom), 10);
	RelationBTreeIterator iterator;
	size32 nTuplesToDelete = 0;
	RelationBTreeIterate(tree, queryTuple, nInputs, &iterator);
	while(RelationBTreeIteratorNext(&iterator)) {
		BTreeTuple const * tuple = BTreeIteratorPeekItem(&(iterator.treeIterator));
		if(tuple->identified == identified) {
			ResizingArrayAppend(&tuplesArray, tuple->atoms);
			nTuplesToDelete++;
		}
	}
	RelationBTreeIteratorEnd(&iterator);

	// Release all atoms referenced by tuples.
	// NOTE: this cannot be done while iterating over the tree,
	// as IFactRelease() calls RelationBTreeRemoveTuples() recursively.
	for(index32 i = 0; i < nTuplesToDelete; i++) {
		Atom * tuple = ResizingArrayGetElement(&tuplesArray, i);
		for(index32 j = 0; j < tree->nColumns; j++) {
			// identified atoms can only be released by calling IFactRelease()
			if(!identified || (j + 1) != identified) {
				TypedAtom atom = TypedTupleGetElement(tuple, j);
				ReleaseTypedAtom(atom);
			}
		}
	}
	// delete tuples
	for(index32 i = 0; i < nTuplesToDelete; i++) {
		TypedTuple * tuple = ResizingArrayGetElement(&tuplesArray, i);
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
	while(RelationBTreeIteratorNext(&iterator)) {
		TypedTuple const * tuple = RelationBTreeIteratorPeekTuple(&iterator);
		// TODO: we should probably not print the full representaiton
		// of identified atoms, as it triggers repeated queries
		TypedTuplePrint(tuple);
		PrintChar('\n');
		nTuples++;
	}
	RelationBTreeIteratorEnd(&iterator);
	PrintF("%u tuples\n", nTuples);
}

/***************************************************************************************************
 * Stubs for using the B-tree service provider.
 * NOTE: this could go to a separate compilation unit
 * 
 * The service context data holds a RelationBTreeIterator.
 */
static void serviceSetupContext(ServiceContext * context)
{
	RelationBTree * btree = context->service->impl.machine.providerData;
	RelationBTreeIterator * iterator = (RelationBTreeIterator *) &context->data;
	RelationBTreeIterate(btree, context->arguments, iterator);
}


static bool serviceCall(ServiceContext * context)
{
	RelationBTreeIterator * iterator = (RelationBTreeIterator *) &context->data;
	bool hasTuple = RelationBTreeIteratorNext(iterator);
	if(hasTuple)
		TypedTupleCopy(RelationBTreeIteratorPeekTuple(iterator), context->arguments);
	return hasTuple;
}


static void serviceFinalizeContext(ServiceContext * context)
{
	RelationBTreeIterator * iterator = (RelationBTreeIterator *) context->data;
	RelationBTreeIteratorEnd(iterator);
}

/**
 * Stubs for the B-tree agent provider
 */
void agentAddTuple(void * agentData, TypedTuple const * arguments)
{
	RelationBTreeAddTuple((BTree *) agentData, arguments);
}


void agentRemoveTuples(void * agentData, TypedTuple const * arguments)
{
	RelationBTreeRemoveTuples((BTree *) agentData, arguments, REMOVE_NORMAL);
}


bool agentIsEmpty(void * agentData)
{
	return BTreeNItems((BTree *) agentData) == 0;
}


void agentTeardown(void * agentData)
{
	FreeRelationBTree((BTree *) agentData);
}


MachineServiceProvider bTreeServiceProvider = {
	.setupContext = &serviceSetupContext,
	.call = &serviceCall,
	.finalizeContext = &serviceFinalizeContext,
	.contextSize = sizeof(RelationBTreeIterator)
};

/**
 * TODO: create AgentHandler
 */

 /*
	.addTuple = &serviceAddTuple,
	.removeTuples = &serviceRemoveTuples,
	.isEmpty = &serviceIsEmpty,
	.teardown = &serviceTeardown,
 */
