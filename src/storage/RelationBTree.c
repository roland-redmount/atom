/**
 * A relation table based on a B-tree. Relies on btree.c for implementation.
 */

#include "btree/btree.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
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
	size8 nColumns, uint8 idPosition, index8 nAtomsPresent, Atom const atoms[], index8 const indexColumns[])
{
	BTreeTuple * btreeTuple = Allocate(btreeTupleNBytes(nColumns));
	btreeTuple->nAtoms = nColumns;
	btreeTuple->idPosition = idPosition;
	btreeTuple->nAtomsPresent = nAtomsPresent;
	// Store atoms in index column order
	for(index8 i = 0; i < nColumns; i++)
		btreeTuple->atoms[i] = atoms[indexColumns[i]];
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


RelationBTree * CreateRelationBTree(size8 nColumns, index8 const indexColumns[])
{
	RelationBTree * relation = Allocate(sizeof(RelationBTree));
	relation->nColumns = nColumns;
	relation->indexColumns = Allocate(nColumns);
	CopyMemory(indexColumns, relation->indexColumns, nColumns);
	relation->btree = BTreeCreate(btreeTupleNBytes(nColumns), btreeCompareItems, 0);
	return relation;
}


size32 RelationBTreeNRows(RelationBTree const * relation)
{
	return BTreeNItems(relation->btree);
}


void FreeRelationBTree(RelationBTree * relation)
{
	Free(relation->indexColumns);
	BTreeFree(relation->btree);
	Free(relation);
}


byte RelationBTreeAddTuple(RelationBTree * relation, Atom const tuple[], uint8 idPosition)
{
	// NOTE: if we are to query for tuples based on idPosition, it must be the leading column
	
	BTreeTuple * btreeTuple = createBTreeTuple(
		relation->nColumns, idPosition, relation->nColumns, tuple, relation->indexColumns);
	byte result = BTreeInsert(relation->btree, btreeTuple);
	Free(btreeTuple);
	return result;
}


byte RelationBTreeRemoveTuple(RelationBTree * relation, Atom const tuple[], uint8 idPosition)
{
	ASSERT(!BTreeIsWriteLocked(relation->btree))

	// NOTE: the below logic is common to all relation tables, could be moved to RelationTable?
	// This would require a method to get the idPosition of a stored tuple from the RelationTableProvider.

	// retrieve the stored tuple to inspect its idPosition
	BTreeTuple * queryTuple = createBTreeTuple(
		relation->nColumns, 0, relation->nColumns, tuple, relation->indexColumns);
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
		iterator->queryTuple = createBTreeTuple(
			relation->nColumns, 0, nInputs, queryTuple, relation->indexColumns);
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
	for(index8 i = 0; i < iterator->relation->nColumns; i++)
		tuple[iterator->relation->indexColumns[i]] = btreeTuple->atoms[i];
}


void RelationBTreeIteratorEnd(RelationBTreeIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->treeIterator));
	if(iterator->queryTuple)
		freeBTreeTuple(iterator->queryTuple);
	SetMemory(iterator, sizeof(RelationBTreeIterator), 0);
}


//--------------------------- MachineOperatorProvider interface ---------------------------------


// Could not fit this in the 8-byte Operator.impl.machine.providerData field :-/
typedef struct s_RelationBTreeOperatorData {
	void * storage;
	index8 nInputs;
} RelationBTreeOperatorData;


static void btreeSetupContext(OperatorContext * context)
{
	RelationBTreeOperatorData * operatorData = context->op->impl.machine.providerData;
	// Initialize the RelationBTreeIterator, allocated by OperatorCreateContext()
	RelationBTreeIterator * iterator = (RelationBTreeIterator *) &context->data;
	RelationBTreeIterate(
		operatorData->storage, context->arguments, operatorData->nInputs, iterator);
}


static bool btreeCall(OperatorContext * context)
{
	RelationBTreeIterator * iterator = (RelationBTreeIterator *) &context->data;
	bool hasTuple = RelationBTreeIteratorNext(iterator);
	if(hasTuple)
		RelationBTreeIteratorGetTuple(iterator, context->arguments);
	return hasTuple;
}


static void btreeFinalizeContext(OperatorContext * context)
{
	RelationBTreeIterator * iterator = (RelationBTreeIterator *) context->data;
	RelationBTreeIteratorEnd(iterator);
}


static void finalizeBTreeOperator(Operator * op)
{
	RelationBTreeOperatorData * operatorData = op->impl.machine.providerData;
	Free(operatorData);
}


MachineOperatorProvider bTreeOperatorProvider = {
	.setupContext = &btreeSetupContext,
	.call = &btreeCall,
	.finalizeContext = &btreeFinalizeContext,
	.finalizeOperator = &finalizeBTreeOperator
};


//--------------------------------------- StorageProvider interface ---------------------------------


static void * btreeCreateStorage(
	index8 const * indexColumns, size8 nColumns, void * table, CreateServiceCallback callback)
{
	// the storage is a RelationBTree struct
	RelationBTree * relationBTree = CreateRelationBTree(nColumns, indexColumns);
	// We will have one operator for each prefix key
	for(index8 nInputs = 0; nInputs <= nColumns; nInputs++) {
		byte parameterIO[nColumns];
		for(index8 i = 0; i < nColumns; i++) {
			if(i < nInputs)
				parameterIO[relationBTree->indexColumns[i]] = PARAMETER_IN;
			else
				parameterIO[relationBTree->indexColumns[i]] = PARAMETER_OUT;
		}
		RelationBTreeOperatorData * operatorData = Allocate(sizeof(RelationBTreeOperatorData));
		operatorData->storage = relationBTree;
		operatorData->nInputs = nInputs;
		// Let RelationTable create the operator and register the service.
		// The operator context data holds a RelationBTreeIterator.
		callback(
			table, &bTreeOperatorProvider, operatorData,
			sizeof(RelationBTreeIterator), CreateIOSignature(parameterIO, nColumns)
		);
	}
	return relationBTree;
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


static void freeRelationBTree(void * storage)
{
	FreeRelationBTree((RelationBTree *) storage);
}


StorageProvider btreeStorageProvider = {
	.createStorage = btreeCreateStorage,
	.addTuple = relationBTreeAddTuple,
	.removeTuple = relationBTreeRemoveTuple,
	.numberOfTuples = relationBTreeNTuples,
	.free = freeRelationBTree,
};
