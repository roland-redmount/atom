/**
 * A relation table based on a B-tree. Relies on btree.c for implemention.
 * Provides both services (query) and agents (assert)
 */

#include "btree/btree.h"
#include "kernel/service.h"
#include "kernel/ifact.h"
#include "kernel/Parameter.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationTable.h"
#include "kernel/typedtuple.h"
#include "lang/TypedAtom.h"
#include "lang/Variable.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "util/ResizingArray.h"


/**
 * The B-tree must store both the Atom array and the column number of
 * an identified atom, if any.
 */
typedef struct s_BTreeTuple {
	size8 nAtoms;
	// 1-based index of the identified atom in the tuple, or 0 if none.
	// Not used for comparing tuples.
	uint8 identified;
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

static size8 btreeTupleNAtoms(size32 nBytes)
{
	return (nBytes - sizeof(BTreeTuple)) / sizeof(Atom);
}

static BTreeTuple * createBTreeTuple(size8 nAtoms, uint8 identified, index8 nAtomsPresent, Atom const * atoms)
{
	BTreeTuple * btreeTuple = Allocate(btreeTupleNBytes(nAtoms));
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

static int8 compareQuery(BTreeTuple const * tuple, BTreeTuple const * query)
{
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


static int8 btreeCompareItems(void const * item, void const * queryItem, size32 itemSize)
{
	return compareQuery((BTreeTuple const *) item, (BTreeTuple const *) queryItem);
}

// Could not fit this in the 8-byte Service.impl.machine.providerData field :-/
typedef struct s_BTreeServiceData {
	RelationTable const * table;
	index8 nInputs;
} BTreeServiceData;


BTree * CreateRelationBTree(size8 nColumns, byte const atomTypes[])
{
	return BTreeCreate(btreeTupleNBytes(nColumns), btreeCompareItems, 0);
}


size8 RelationBTreeNColumns(BTree const * tree)
{
	return btreeTupleNAtoms(tree->itemSize);
}


size32 RelationBTreeNRows(BTree const * tree)
{
	return BTreeNItems(tree);
}


void FreeRelationBTree(BTree * tree)
{
	BTreeFree(tree);
}


byte RelationBTreeAddTuple(BTree * btree, Atom const tuple[], uint8 idPosition)
{
	// NOTE: this could be done with C99 VLA stack allocation
	size8 nColumns = btreeTupleNAtoms(btree->itemSize);
	BTreeTuple * btreeTuple = createBTreeTuple(nColumns, idPosition, nColumns, tuple);
	byte result = BTreeInsert(btree, btreeTuple);
	Free(btreeTuple);
	return result;
}


static byte relationBTreeRemoveTuple(RelationTable const * table, Atom const tuple[])
{
	BTree * btree = table->data;
	ASSERT(!BTreeIsWriteLocked(btree))
	BTreeTuple * btreeTuple = createBTreeTuple(table->nColumns, 0, table->nColumns, tuple);
	BTreeDeleteResult result = BTreeDelete(btree, btreeTuple);
	Free(btreeTuple);
	return result == BTREE_DELETED ? TUPLE_REMOVED : TUPLE_PROTECTED;
}

/**
 * Stubs for the RelationTableProvider interface
 */

static void * createRelationBTree(size8 nColumns, byte const atomTypes[])
{
	return CreateRelationBTree(nColumns, atomTypes);
}

static size32 relationBTreeNTuples(RelationTable const * table)
{
	return RelationBTreeNRows(table->data);
}

static byte relationBTreeAddTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	return RelationBTreeAddTuple(table->data, tuple, idPosition);
}

static void freeRelationBTree(RelationTable * table)
{
	FreeRelationBTree(table->data);
}

RelationTableProvider btreeTableProvider = {
	.createTable = createRelationBTree,
	.addTuple = relationBTreeAddTuple,
	.removeTuple = relationBTreeRemoveTuple,
	.numberOfTuples = relationBTreeNTuples,
	.free = freeRelationBTree,
};


/*
 * NOTE: to support searching with variables when using an untyped tuple,
 * all atoms except 0 ...  nInputs-1 are considered variables and are
 * ignored by the B-tree comparison function.
 * 
 * NOTE: This does not support queries with repeated variables like (a x b y z y) !
 * For this, the service must identify parameters, e.g. (a @1 b @2 c @2) so that
 * we can check for equality. Compare with TypedTupleMatch()
 * We might handle this with a permutation, (a @1 b@1) <- (a @1 b @2 c @2) ?
 * Not clear to me if there is a value in having services with repeated parameters
 * (as opposed to rules with repeated variables, which is necessary for joins).
 */

void RelationBTreeIterate(
	BTree * btree, Atom const queryTuple[], size8 nInputs, RelationBTreeIterator * iterator)
{
	SetMemory(iterator, sizeof(RelationBTreeIterator), 0);
	size8 nColumns = btreeTupleNAtoms(btree->itemSize);
	if(queryTuple) {
		iterator->queryTuple = createBTreeTuple(nColumns, 0, nInputs, queryTuple);
	}
	else
		iterator->queryTuple = 0;
	BTreeIterate(&(iterator->treeIterator), btree);
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
	else
		return BTreeIteratorNext(&(iterator->treeIterator));
	return false;
}


Atom RelationBTreeIteratorGetAtom(RelationBTreeIterator const * iterator, index8 i)
{
	size8 nColumns = btreeTupleNAtoms(iterator->treeIterator.btree->itemSize);
	ASSERT(i < nColumns);
	BTreeTuple const * tuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
	return tuple->atoms[i];
}


void RelationBTreeIteratorGetTuple(RelationBTreeIterator const * iterator, Atom tuple[])
{
	BTreeTuple const * btreeTuple = BTreeIteratorPeekItem(&(iterator->treeIterator));
	CopyMemory(btreeTuple->atoms, tuple, btreeTuple->nAtoms * sizeof(Atom));
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
		freeBTreeTuple(iterator->queryTuple);
	SetMemory(iterator, sizeof(RelationBTreeIterator), 0);
}

/*  TODO: these convenience functions might be moved to service / service registry? */

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
 * Removing multiple tuples corresponds to retracting a (large) set of facts,
 * for example when deleting an entire list of items (list @l element _ position _).
 * RetractFact() currently does not support this, nor does RelationTableProvider.removeTuple()
 * 
 * This does occurs when releasing an ifact; see open issue in IFactRelease() -- must be resolved
 * There is also the thorny issue of recursive deletions via IFactRelease().
 */
size32 RelationBTreeRemoveTuples(BTree * btree, Atom const queryTuple[], size8 nInputs, uint8 identified)
{
	ASSERT(!BTreeIsWriteLocked(btree));
	size8 nColumns = btreeTupleNAtoms(btree->itemSize);

	// retrieve all matching tuples
	ResizingArray tuplesArray;
	CreateResizingArray(&tuplesArray, btreeTupleNBytes(nColumns), 10);
	RelationBTreeIterator iterator;
	size32 nTuplesToDelete = 0;
	RelationBTreeIterate(btree, queryTuple, nInputs, &iterator);
	while(RelationBTreeIteratorNext(&iterator)) {
		BTreeTuple const * tuple = BTreeIteratorPeekItem(&(iterator.treeIterator));
		if(tuple->identified == identified) {
			ResizingArrayAppend(&tuplesArray, tuple);
			nTuplesToDelete++;
		}
	}
	RelationBTreeIteratorEnd(&iterator);

	// Release all atoms referenced by tuples.
	// NOTE: this cannot be done while iterating over the tree,
	// as IFactRelease() calls RelationBTreeRemoveTuples() recursively.
	for(index32 i = 0; i < nTuplesToDelete; i++) {
		BTreeTuple * tuple = ResizingArrayGetElement(&tuplesArray, i);
		for(index32 j = 0; j < nColumns; j++) {
			ReleaseTypedAtom(CreateTypedAtom(table->atomTypes[j], tuple->atoms[j]));
		}
	}
	// delete tuples
	for(index32 i = 0; i < nTuplesToDelete; i++) {
		BTreeTuple * tuple = ResizingArrayGetElement(&tuplesArray, i);
		ASSERT(BTreeDelete(btree, tuple) == BTREE_DELETED);
	}
	FreeResizingArray(&tuplesArray);
	return nTuplesToDelete;
}


// for debugging
void RelationBTreeDump(RelationTable * table)
{
	BTree * btree = table->data;
	ASSERT(btree)
	PrintF("BTree %u columns\n", table->nColumns);

	RelationBTreeIterator iterator;
	RelationBTreeIterate(table, 0, 0, &iterator);
	size32 nTuples = 0;
	while(RelationBTreeIteratorNext(&iterator)) {
		Atom const * tuple = RelationBTreeIteratorPeekTuple(&iterator);
		// TODO: we should probably not print the full representaiton
		// of identified atoms, as it triggers repeated queries
		PrintTuple(table->atomTypes, tuple, table->nColumns);
		PrintChar('\n');
		nTuples++;
	}
	RelationBTreeIteratorEnd(&iterator);
	PrintF("%u tuples\n", nTuples);
}

/***************************************************************************************************
 * Stubs for using the B-tree service provider.
 * 
 * The service context data holds a RelationBTreeIterator.
 */
static void serviceSetupContext(ServiceContext * context)
{
	// 
	BTreeServiceData * serviceData = context->service->impl.machine.providerData;
	// Space for RelationBTreeIterator, allocated by ServiceCreateContext()
	RelationBTreeIterator * iterator = (RelationBTreeIterator *) &context->data;

	RelationBTreeIterate(serviceData->table, context->arguments, serviceData->nInputs, iterator);
}


static bool serviceCall(ServiceContext * context)
{
	RelationBTreeIterator * iterator = (RelationBTreeIterator *) &context->data;
	bool hasTuple = RelationBTreeIteratorNext(iterator);
	if(hasTuple) {
		CopyMemory(
			RelationBTreeIteratorPeekTuple(iterator),
			context->arguments,
			iterator->table->nColumns * sizeof(Atom)
		);
	}
	return hasTuple;
}


static void serviceFinalizeContext(ServiceContext * context)
{
	RelationBTreeIterator * iterator = (RelationBTreeIterator *) context->data;
	RelationBTreeIteratorEnd(iterator);
}

static void finalizeBTreeService(Service * service)
{
	// Deallocate the BTreeServiceData structure
	Free(service->impl.machine.providerData);
}

MachineServiceProvider bTreeServiceProvider = {
	.setupContext = &serviceSetupContext,
	.call = &serviceCall,
	.finalizeContext = &serviceFinalizeContext,
	.finalizeService = &finalizeBTreeService,
	.contextSize = sizeof(RelationBTreeIterator)
};


RelationTable const * CreateRelationBTreeWithServices(Atom form, size8 nColumns, byte const atomTypes[])
{
	// register
	RelationTable const * table = CreateRelationTable(&btreeTableProvider, form, nColumns, atomTypes);

	// register services with leading columns as inputs
	byte parameterIO[nColumns];
	for(index8 i = 0; i < nColumns; i++)
		parameterIO[i] = PARAMETER_OUT;
	for(index8 i = 0; i < nColumns; i++) {
		parameterIO[i] = PARAMETER_IN;
		BTreeServiceData * serviceData = Allocate(sizeof(BTreeServiceData));
		serviceData->table = table;
		serviceData->nInputs = i + 1;
		Service * service = CreateMachineService(nColumns, &bTreeServiceProvider, serviceData);
		RelationAddService(table, parameterIO, service);
	}
	return table;
}
