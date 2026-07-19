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


typedef struct s_BTreeTuple {
	size8 nAtoms;
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


static int8 btreeCompareItems(void const * item, void const * queryItem, void * data)
{
	return compareQuery((TypedTuple const *) item, (TypedTuple const *) queryItem);
}

// Could not fit this in the 8-byte Service.impl.machine.providerData field :-/
typedef struct s_BTreeServiceData {
	BTree * btree;
	index8 nInputs;
} BTreeServiceData;

static void * createRelationBTree(Atom form, size8 nColumns, byte const atomTypes[])
{
	BTree * btree = BTreeCreate(btreeTupleNBytes(nColumns), btreeCompareItems, 0);

	// register services with leading columns as inputs
	Atom parameters[nColumns];
	// initialize all parameters to outputs
	for(index8 i = 0; i < nColumns; i++) {
		parameters[i] = (Atom) {
			.parameter = {
				 .atomType = atomTypes[i],
				 .io = PARAMETER_OUT,
				 .number = i+1
			}
		};
	}
	for(index8 i = 0; i < nColumns; i++) {
		parameters[i].parameter.io = PARAMETER_IN;
		BTreeServiceData * serviceData = Allocate(sizeof(BTreeServiceData));
		serviceData->btree = btree;
		serviceData->nInputs = i + 1;
		Service * service = CreateMachineService(nColumns, &bTreeServiceProvider, serviceData);
		RegistryAddService(form, parameters, service);
	}
	return btree;
}


static void freeRelationBTree(RelationTable * table)
{
	BTree * btree = table->data;
	BTreeFree(btree);
}


static size32 relationBTreeNTuples(RelationTable * table)
{
	BTree * btree = table->data;
	return BTreeNItems(btree);
}


static byte relationBTreeAddTuple(RelationTable * table, Atom const tuple[], uint8 idPosition)
{
	BTree * btree = table->data;
	// NOTE: this could be done with C99 VLA stack allocation
	BTreeTuple * btreeTuple = createBTreeTuple(table->nColumns, idPosition, table->nColumns, tuple);
	byte result = BTreeInsert(btree, tuple);
	Free(btreeTuple);
	return result;
}


static void relationBTreeRemoveTuple(RelationTable * table, Atom const tuple[], uint8 identified)
{
	BTree * btree = table->data;
	ASSERT(!BTreeIsWriteLocked(btree))
	ASSERT(BTreeDelete(btree, tuple) == BTREE_DELETED)
}


RelationTableProvider btreeTableProvider = {
	.createTable = createRelationBTree,
	.addTuple = relationBTreeAddTuple,
	.removeTuple = relationBTreeRemoveTuple,
	.numberOfTuples = relationBTreeNTuples,
	.free = freeRelationBTree,
};


/*
 * TODO: to support searching with variables when using an untyped Tuple,
 * we could set all variable atoms to zero and start iteration at the first item
 * that compares >= to the tuple; this requires modifying BTreeIteratorSeek()
 * Then we do linear search which doesn't require BTree to compare tuples.
 * A typed service would treat output parameters as variables.
 */

void RelationBTreeIterate(
	RelationTable * table, Atom const * queryTuple, size8 nInputs, RelationBTreeIterator * iterator)
{
	SetMemory(iterator, sizeof(RelationBTreeIterator), 0);
	iterator->table = table;
	if(queryTuple) {
		iterator->queryTuple = createBTreeTuple(table->nColumns, 0, nInputs, queryTuple);
	}
	else
		iterator->queryTuple = 0;
	BTreeIterate(&(iterator->treeIterator), (BTree *) table->data);
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
	ASSERT(i < iterator->table->nColumns);
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

/*  TODO: these convenience functions might be moved to service / service registry? */

void RelationBTreeQuerySingle(RelationTable * table, Atom const * queryTuple, size8 nInputs, Atom * resultTuple)
{
	BTree * btree = table->data;
 	RelationBTreeIterator iterator;
 	RelationBTreeIterate(btree, queryTuple, nInputs, &iterator);
 	ASSERT(RelationBTreeIteratorNext(&iterator));
 	RelationBTreeIteratorGetTuple(&iterator, resultTuple);
 	// verify the relation has a single tuple only
 	ASSERT(!RelationBTreeIteratorNext(&iterator));
 	RelationBTreeIteratorEnd(&iterator);
}


Atom RelationBTreeQuerySingleAtom(RelationTable * table, Atom const * queryTuple, size8 nInputs, index8 index)
{
 	BTree * btree = table->data;
 	RelationBTreeIterator iterator;
 	RelationBTreeIterate(btree, queryTuple, nInputs, &iterator);
 	ASSERT(RelationBTreeIteratorNext(&iterator));
 	Atom atom = RelationBTreeIteratorGetAtom(&iterator, index);
 	// verify the relation has a single tuple only
 	ASSERT(!RelationBTreeIteratorNext(&iterator));
 	RelationBTreeIteratorEnd(&iterator);
	return atom;
}


size32 RelationBTreeRemoveTuples(RelationTable * table, Atom const * queryTuple, size8 nInputs, uint8 identified)
{
	BTree * btree = table->data;
	ASSERT(!BTreeIsWriteLocked(btree));

	// retrieve all matching tuples
	ResizingArray tuplesArray;
	CreateResizingArray(&tuplesArray, table->nColumns * sizeof(Atom), 10);
	RelationBTreeIterator iterator;
	size32 nTuplesToDelete = 0;
	RelationBTreeIterate(table, queryTuple, nInputs, &iterator);
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
		for(index32 j = 0; j < table->nColumns; j++) {
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
	BTreeServiceData * serviceData = context->service->impl.machine.providerData;
	RelationBTreeIterator * iterator = (RelationBTreeIterator *) &context->data;
	RelationBTreeIterate(serviceData->btree, context->arguments, serviceData->nInputs, iterator);
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


/**
 * Stubs for the B-tree agent 
 * TODO: Remove, now handled by RelationTable
 */
/*
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

void RelationBTreeInitialize(void)
{

}
*/