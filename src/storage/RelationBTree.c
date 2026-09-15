/**
 * A relation table based on a B-tree. Relies on btree.c for implementation.
 */

#include "btree/btree.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "library/MachineService.h"
#include "storage/StorageProvider.h"
#include "storage/RelationBTree.h"
#include "memory/allocator.h"


/**
 * A B-tree item stores the tuple Atom array, permuted according to the column order,
 * and the column number (not permuted) of an identified atom, if any.
 */
typedef struct s_BTreeTuple {
	size8 nAtoms;
	// 1-based position of the identified atom in the tuple, or 0 if none.
	// Not used for comparing tuples.
	uint8 idPosition;
	// Only atoms[0] .. atoms[nAtomsPresent-1] are stored in the tuple.
	// This is = nAtoms in B-tree items (full tuple is stored),
	// but < nAtoms in queries as output arguments are not present.
	index8 nAtomsPresent;
	Atom atoms[];
} BTreeTuple;


static size32 btreeTupleNBytes(size8 nAtoms)
{
	return sizeof(BTreeTuple) + nAtoms * sizeof(Atom);
}


static BTreeTuple * createBTreeTuple(
	size8 nColumns, uint8 idPosition, index8 nAtomsPresent, Atom const atoms[])
{
	BTreeTuple * btreeTuple = Allocate(btreeTupleNBytes(nColumns));
	btreeTuple->nAtoms = nColumns;
	btreeTuple->idPosition = idPosition;
	btreeTuple->nAtomsPresent = nAtomsPresent;
	TupleCopy(atoms, btreeTuple->atoms, nColumns);
	return btreeTuple;
}


static void freeBTreeTuple(BTreeTuple * btreeTuple)
{
	Free(btreeTuple);
}


/**
 * Comparison function used to compare a B-tree tuple to another tuple or key.
 * If n = tupleOrKey->nAtomsPresent is less than the tuple length, only the n
 * leftmost atoms (leading columns) are used for the comparison, while the remaining
 * columns are considered as variables. Otherwise, the comparison is the same as TupleCompare().
 * This does not handle query tuples like (a _ b).
 */
static int8 compareBTreeTuples(BTreeTuple const * tuple, BTreeTuple const * tupleOrKey)
{
	return TupleCompare(tuple->atoms, tupleOrKey->atoms, tupleOrKey->nAtomsPresent);
}


static int8 btreeCompareItems(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareBTreeTuples((BTreeTuple const *) item, (BTreeTuple const *) itemOrKey);
}


RelationBTree * CreateRelationBTree(size8 nColumns)
{
	RelationBTree * relation = Allocate(sizeof(RelationBTree));
	relation->nColumns = nColumns;
	relation->btree = BTreeCreate(btreeTupleNBytes(nColumns), btreeCompareItems, 0);
	return relation;
}


size32 RelationBTreeNRows(RelationBTree const * relation)
{
	return BTreeNItems(relation->btree);
}


void FreeRelationBTree(RelationBTree const * relation)
{
	BTreeFree(relation->btree);
	Free(relation);
}


byte RelationBTreeAddTuple(RelationBTree * relation, Atom const tuple[], uint8 idPosition)
{
	// NOTE: if we are to query for tuples based on idPosition, it must be the leading column
	
	BTreeTuple * btreeTuple = createBTreeTuple(
		relation->nColumns, idPosition, relation->nColumns, tuple);
	byte result = BTreeInsert(relation->btree, btreeTuple);
	Free(btreeTuple);
	return result;
}


byte RelationBTreeRemoveTuple(RelationBTree * relation, Atom const tuple[], uint8 idPosition)
{
	ASSERT(!BTreeIsWriteLocked(relation->btree))

	// NOTE: the below logic is common to all relation tables, could be moved to RelationWriter?
	// This would require a method to get the idPosition of a stored tuple from the RelationTableProvider.

	// retrieve the stored tuple to inspect its idPosition
	BTreeTuple * queryTuple = createBTreeTuple(
		relation->nColumns, 0, relation->nColumns, tuple);
	BTreeTuple * btreeTuple = BTreePeekItem(relation->btree, queryTuple);
	Free(queryTuple);
	if(!btreeTuple)
		return TUPLE_NOT_FOUND;
	
	if(idPosition && (btreeTuple->idPosition != idPosition)) {
		// The specified idPosition is wrong, indicating an internal error
		ASSERT(false)
	}
	if(!idPosition && btreeTuple->idPosition) {
		// Attempt to retract an identifying fact
		return TUPLE_PROTECTED;
	}
	
	// TODO: we should probably have a BTreeDeleteAt(void * item) function that accepts a direct
	// pointer to a stored item, to avoid re-running the search
	ASSERT(BTreeDelete(relation->btree, btreeTuple, 0) == BTREE_DELETED)
	return TUPLE_REMOVED;
}

/*
 * NOTE: to support searching with variables when using an untyped tuple,
 * all atoms except 0 ...  nInputs-1 are considered variables and are
 * ignored by the B-tree comparison function.
 * 
 * NOTE: This does not support queries with repeated variables like (a x b y z y) !
 * For this, the operator must identify parameters, e.g. (a @1 b @2 c @2) so that
 * we can check for equality.
 * We might handle this with a permutation, (a @1 b@1) <- (a @1 b @2 c @2) ?
 * Not clear to me if there is a value in having operators with repeated parameters
 * (as opposed to rules with repeated variables, which is necessary for joins).
 */

void RelationBTreeIterate(
	RelationBTree * relation, Atom const queryTuple[], size8 nInputs, RelationBTreeIterator * iterator)
{
	iterator->relation = relation;
	if(queryTuple) {
		iterator->queryTuple = createBTreeTuple(relation->nColumns, 0, nInputs, queryTuple);
	}
	else
		iterator->queryTuple = 0;
	BTreeIterate(&(iterator->treeIterator), relation->btree);
}


bool RelationBTreeIteratorNext(RelationBTreeIterator * iterator)
{
	if(!iterator->queryTuple)
		return BTreeIteratorNext(&(iterator->treeIterator));
	// else we have a query tuple
	if(BTreeIteratorBeforeFirst(&iterator->treeIterator)) {
		// new iterator, seek to first match
		return BTreeIteratorSeek(&(iterator->treeIterator), iterator->queryTuple);
	}
	else {
		if(BTreeIteratorNext(&(iterator->treeIterator))) {
			// return tuples as long as they match
			BTreeTuple const * btreeTuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
			return (compareBTreeTuples(btreeTuple, iterator->queryTuple) == 0);
		}
	}	
	return false;
}


Atom RelationBTreeIteratorGetAtom(RelationBTreeIterator const * iterator, index8 i)
{
	ASSERT(i < iterator->relation->nColumns);
	BTreeTuple const * tuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
	return tuple->atoms[i];
}


void RelationBTreeIteratorGetTuple(RelationBTreeIterator const * iterator, Atom tuple[])
{
	BTreeTuple const * btreeTuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
	TupleCopy(btreeTuple->atoms, tuple, iterator->relation->nColumns);
}


void RelationBTreeIteratorEnd(RelationBTreeIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->treeIterator));
	if(iterator->queryTuple)
		freeBTreeTuple(iterator->queryTuple);
	SetMemory(iterator, sizeof(RelationBTreeIterator), 0);
}


//--------------------------- MachineOperatorProvider interface ---------------------------------

// A pointer-sized union, to store nInputs in the readerData pointer
typedef union {
	index8 nInputs;
	void * ptr;
} RelationBTreeOperatorData;


static void btreeSetupState(void * state, Atom arguments[], void * readerData, void * storage)
{
	RelationBTreeOperatorData bTreeOperatorData;
	bTreeOperatorData.ptr = readerData;
	RelationBTree * relationBTree = storage;
	// Initialize the RelationBTreeIterator, allocated by OperatorCreateContext()
	RelationBTreeIterator * iterator = state;
	RelationBTreeIterate(relationBTree, arguments, bTreeOperatorData.nInputs, iterator);
}


static bool btreeCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	RelationBTreeIterator * iterator = state;
	bool hasTuple = RelationBTreeIteratorNext(iterator);
	if(hasTuple)
		RelationBTreeIteratorGetTuple(iterator, arguments);
	return hasTuple;
}


static void btreeFinalizeState(void * state, void * readerData, void * storage)
{
	RelationBTreeIterator * iterator = state;
	RelationBTreeIteratorEnd(iterator);
}


//--------------------------------------- StorageProvider interface ---------------------------------


/**
 * Create implementation for a new relation
 */
static void * btreeSetupStorage(size8 nColumns, size32 * nReaders)
{
	// One reader per prefix key
	*nReaders = nColumns + 1;
	return CreateRelationBTree(nColumns);
}


/**
 * Return a specific reader, from 0, ..., n.
 * The reader is described by an IOSignature and function pointers to call;
 * also need reader-specific data, here the number of leading columns (RelationBTreeOperatorData)
 */
static void setupReader(RelationReaderSpec * spec, index32 readerIndex, void * storage)
{
	// We will create one operator for each prefix key
	size8 nInputs = readerIndex + 1;
	RelationBTree * relationBTree = storage;
	byte parameterIO[relationBTree->nColumns];
	for(index8 i = 0; i < relationBTree->nColumns; i++) {
		if(i < nInputs)
			parameterIO[i] = PARAMETER_IN;
		else
			parameterIO[i] = PARAMETER_OUT;
	}
	RelationBTreeOperatorData readerData = {.nInputs = nInputs};

	spec->ioSignature = CreateIOSignature(parameterIO, relationBTree->nColumns);
	spec->stateSize = sizeof(RelationBTreeIterator);
	spec->readerData = readerData.ptr;
	spec->setupState = &btreeSetupState;
	spec->call = &btreeCall;
	spec->finalizeState = &btreeFinalizeState;
	// no finalizeReader()
}


static size32 relationBTreeNTuples(void * storage)
{
	return RelationBTreeNRows((RelationBTree *) storage);
}


static byte relationBTreeAddTuple(void * storage, Atom const tuple[], uint8 idPosition)
{
	return RelationBTreeAddTuple((RelationBTree *) storage, tuple, idPosition);
}


static byte relationBTreeRemoveTuple(void * storage, Atom const tuple[], uint8 idPosition)
{
	return RelationBTreeRemoveTuple((RelationBTree *) storage, tuple, idPosition);
}


static void relationBTreeFree(void * storage)
{
	FreeRelationBTree((RelationBTree *) storage);
}


StorageProvider btreeStorageProvider = {
	.setupStorage = btreeSetupStorage,
	.addTuple = relationBTreeAddTuple,
	.removeTuple = relationBTreeRemoveTuple,
	.numberOfTuples = relationBTreeNTuples,
	.free = relationBTreeFree,
};
