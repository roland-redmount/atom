/**
 * A relation table based on a B-tree. Relies on btree.c for implemention.
 * Provides both operators (query) and agents (assert)
 */

#include "btree/btree.h"
#include "kernel/operator.h"
#include "kernel/ifact.h"
#include "kernel/Parameter.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "lang/TermForm.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/typedtuple.h"
#include "lang/TypedAtom.h"
#include "lang/Variable.h"
#include "memory/allocator.h"
#include "memory/pool.h"
#include "util/ResizingArray.h"



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
	size8 nColumns, uint8 idPosition, index8 nAtomsPresent, Atom const * atoms, index8 const indexColumns[])
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


RelationBTree * CreateRelationBTree(size8 nColumns, byte const atomTypes[], index8 const indexColumns[])
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
	return CreateRelationBTree(
		table->relation->nColumns, table->relation->atomTypes, table->indexColumns);
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

/*  TODO: these convenience functions might be moved to the service registry? */

// void RelationBTreeQuerySingle(RelationTable * table, Atom const queryTuple[], size8 nInputs, Atom resultTuple[])
// {
// 	RelationBTreeIterator iterator;
//  	RelationBTreeIterate(table, queryTuple, nInputs, &iterator);
//  	ASSERT(RelationBTreeIteratorNext(&iterator));
//  	RelationBTreeIteratorGetTuple(&iterator, resultTuple);
//  	// verify the relation has a single tuple only
//  	ASSERT(!RelationBTreeIteratorNext(&iterator));
//  	RelationBTreeIteratorEnd(&iterator);
// }


// Atom RelationBTreeQuerySingleAtom(RelationTable * table, Atom const queryTuple[], size8 nInputs, index8 index)
// {
//  	RelationBTreeIterator iterator;
//  	RelationBTreeIterate(table, queryTuple, nInputs, &iterator);
//  	ASSERT(RelationBTreeIteratorNext(&iterator));
//  	Atom atom = RelationBTreeIteratorGetAtom(&iterator, index);
//  	// verify the relation has a single tuple only
//  	ASSERT(!RelationBTreeIteratorNext(&iterator));
//  	RelationBTreeIteratorEnd(&iterator);
// 	return atom;
// }

/**
 * TODO: unclear how this function fits within the RelationTable framework.
 * We now also have relationBTreeRemoveTuple() for removing 1 tuple.
 * With an Atom[] as query, we cannot encode variables; RelatonTable only adds/removes
 * 1 tuple at a time, as does RetractFact().
 * 
 * Removing multiple facts should probably be a query-action combination like
 * 
 * (list @l element e position p) : retract (list @l element e position p)
 *
 * This should compile to a loop where for each tuple provided by the operator,
 * the retract "agent" removes the tuple. A practical problem with this is that
 * many data structures (including our B-tree) does not support deleting elements during
 * iteration; and even if it is supported, it may be very slow, for example in array
 * storage where each delete requires moving O(N) elements, so that deleting the full
 * array is O(N^2). We could implement a special case in RelationTable for removing the
 * entire storage structure though, as this does not require variables.
 * This is a fundamental problem when separating read/write operations into operators and agents.
 * 
 * Removing multiple elements also occurs when releasing an ifact; see open issue in IFactRelease()
 * There is also the thorny issue of recursive deletions via IFactRelease().
 */

// size32 RelationBTreeRemoveTuples(BTree * btree, Atom const queryTuple[], size8 nInputs, uint8 identified)
// {
// 	ASSERT(!BTreeIsWriteLocked(btree));
// 	size8 nColumns = btreeTupleNAtoms(btree->itemSize);

// 	// retrieve all matching tuples
// 	ResizingArray tuplesArray;
// 	CreateResizingArray(&tuplesArray, btreeTupleNBytes(nColumns), 10);
// 	RelationBTreeIterator iterator;
// 	size32 nTuplesToDelete = 0;
// 	RelationBTreeIterate(btree, queryTuple, nInputs, &iterator);
// 	while(RelationBTreeIteratorNext(&iterator)) {
// 		BTreeTuple const * tuple = BTreeIteratorPeekItem(&(iterator.treeIterator));
// 		if(tuple->identified == identified) {
// 			ResizingArrayAppend(&tuplesArray, tuple);
// 			nTuplesToDelete++;
// 		}
// 	}
// 	RelationBTreeIteratorEnd(&iterator);

// 	// Release all atoms referenced by tuples.
// 	// NOTE: this cannot be done while iterating over the tree,
// 	// as IFactRelease() calls RelationBTreeRemoveTuples() recursively.
// 	for(index32 i = 0; i < nTuplesToDelete; i++) {
// 		BTreeTuple * tuple = ResizingArrayGetElement(&tuplesArray, i);
// 		for(index32 j = 0; j < nColumns; j++) {
// 			ReleaseTypedAtom(CreateTypedAtom(table->atomTypes[j], tuple->atoms[j]));
// 		}
// 	}
// 	// delete tuples
// 	for(index32 i = 0; i < nTuplesToDelete; i++) {
// 		BTreeTuple * tuple = ResizingArrayGetElement(&tuplesArray, i);
// 		ASSERT(BTreeDelete(btree, tuple) == BTREE_DELETED);
// 	}
// 	FreeResizingArray(&tuplesArray);
// 	return nTuplesToDelete;
// }


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
	AcquireRelationTable(table);
	providerData->table = table;
	providerData->nInputs = nInputs;
	// Tuples are stored permuted into index column order, so that is the order
	// in which the B-tree yields them
	return CreateMachineOperator(
		table->relation->nColumns, table->indexColumns, &bTreeProvider, providerData,
		sizeof(RelationBTreeIterator));
}


/**
 * A B-tree can search on any prefix of its index column order, and so registers one
 * service per prefix length: the first nInputs columns in index order are inputs and the
 * rest outputs. A signature binding a column out of that order, such as
 * (list< position> element<), is not among them; see ListGetPosition() in list.c
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
		RelationTableAddService(table, parameterIO, op);
		// The registry now holds the reference to the operator
		ReleaseOperator(op);
	}
}


RelationTable * CreateRelationBTreeWithServicesBootstrap(
	Atom termForm, Atom predicateForm, size8 nColumns, byte const atomTypes[], index8 const indexColumns[])
{
	Relation const * relation = RelationRegistryFind(termForm, nColumns, atomTypes);
	if(relation)
		AcquireRelation(relation);
	else
		relation = CreateRelationBootstrap(termForm, predicateForm, nColumns, atomTypes);

	RelationTable * table = CreateRelationTable(relation, &btreeTableProvider, indexColumns);
	// the table holds its own reference to the relation
	ReleaseRelation(relation);
	return table;
}


RelationTable * CreateRelationBTreeWithServices(
	Atom termForm, size8 nColumns, byte const atomTypes[], index8 const indexColumns[])
{
	return CreateRelationBTreeWithServicesBootstrap(
		termForm, TermFormGetPredicateForm(termForm), nColumns, atomTypes, indexColumns);
}

