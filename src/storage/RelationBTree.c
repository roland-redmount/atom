/**
 * A relation table based on a B-tree. Relies on btree.c for implementation.
 */

#include "btree/btree.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/RelationTable.h"
#include "kernel/ServiceRegistry.h"
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

/**
 * Stubs adapting the RelationBTree data structure to the RelationTableProvider interface.
 * RelationBTree keeps its own copy of the arity and the index column order, so that it
 * stays usable on its own; see CreateRelationBTree().
 */

static void * createRelationBTree(RelationTable const * table)
{
	return CreateRelationBTree(table->relation->nColumns, table->indexColumns);
}

static size32 relationBTreeNTuples(RelationTable const * table)
{
	return RelationBTreeNRows((RelationBTree *) table->storage);
}

static byte relationBTreeAddTuple(
	RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	return RelationBTreeAddTuple((RelationBTree *) table->storage, tuple, idPosition);
}

static byte relationBTreeRemoveTuple(
	RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	return RelationBTreeRemoveTuple((RelationBTree *) table->storage, tuple, idPosition);
}


static void freeRelationBTree(RelationTable const * table)
{
	FreeRelationBTree((RelationBTree *) table->storage);
}

static void btreeRegisterServices(RelationTable * table);

RelationTableProvider btreeTableProvider = {
	.createStorage = createRelationBTree,
	.registerServices = btreeRegisterServices,
	.addTuple = relationBTreeAddTuple,
	.removeTuple = relationBTreeRemoveTuple,
	// .removeIFactTuples = relationBTreeRemoveIFactTuples,
	.numberOfTuples = relationBTreeNTuples,
	.free = freeRelationBTree,
};


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


/**
 * Stubs for the B-tree operator provider.
 * 
 * The operator context data holds a RelationBTreeIterator.
 */

// Could not fit this in the 8-byte Operator.impl.machine.providerData field :-/
typedef struct s_RelationBTreeProviderData {
	/**
	 * The table this operator reads. Acquired, so that the storage outlives every
	 * operator reading it: an operator may be shared with a compiled service that knows
	 * nothing of this table, and so survive the table being dropped.
	 */
	RelationTable * table;
	index8 nInputs;
} RelationBTreeProviderData;


static void btreeSetupContext(OperatorContext * context)
{
	RelationBTreeProviderData * providerData = context->op->impl.machine.providerData;
	// Initialize the RelationBTreeIterator, allocated by OperatorCreateContext()
	RelationBTreeIterator * iterator = (RelationBTreeIterator *) &context->data;
	RelationBTreeIterate(
		providerData->table->storage, context->arguments, providerData->nInputs, iterator);
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
	RelationBTreeProviderData * providerData = op->impl.machine.providerData;
	ReleaseRelationTable(providerData->table);
	Free(providerData);
}

MachineProvider bTreeProvider = {
	.setupContext = &btreeSetupContext,
	.call = &btreeCall,
	.finalizeContext = &btreeFinalizeContext,
	.finalizeOperator = &finalizeBTreeOperator
};


/**
 * Create a B-tree operator with nInputs leading input parameters, acquiring the table it
 * reads; see RelationBTreeProviderData.
 */
static Operator * createBTreeOperator(RelationTable * table, size8 nInputs)
{
	RelationBTreeProviderData * providerData = Allocate(sizeof(RelationBTreeProviderData));
	providerData->table = table;
	providerData->nInputs = nInputs;
	// Tuples are stored permuted into index column order, so that is the order
	// in which the B-tree yields them
	return CreateMachineOperator(
		table->relation->nColumns, table->indexColumns, &bTreeProvider, providerData,
		sizeof(RelationBTreeIterator));
}


/**
 * A B-tree can search on any prefix of its index column order, so we registers one primitive
 * service for each prefix, where the first nInputs columns in index order are inputs and the
 * rest outputs.
 * 
 * NOTE: I'm not happy with how this interacts with the ServiceRegistry, which seems like
 * a higher level. Perhaps we instead return (IOSignature, Operators) pairs?
 */
static void btreeRegisterServices(RelationTable * table)
{
	size8 nColumns = table->relation->nColumns;
	for(index8 nInputs = 0; nInputs <= nColumns; nInputs++) {
		Operator * op = createBTreeOperator(table, nInputs);
		byte parameterIO[nColumns];
		for(index8 i = 0; i < nColumns; i++) {
			if(i < nInputs)
				parameterIO[table->indexColumns[i]] = PARAMETER_IN;
			else
				parameterIO[table->indexColumns[i]] = PARAMETER_OUT;
		}
		CreateService(
			table->relation, CreateIOSignature(parameterIO, nColumns), op, SERVICE_PRIMITIVE);
	}
}
