
#include "kernel/operator.h"
#include "kernel/TupleStore.h"
#include "memory/pool.h"

// These are defined in Relation.c
extern void RelationAttachTupleStore(Relation relation, TupleStore * store);
void RelationDetachTupleStore(Relation relation, TupleStore * store);


/**
 * Pool allocation for structures
 */
void * storePool = 0;

static TupleStore * allocateTupleStore(void)
{
	if(!storePool)
		storePool = CreatePool(sizeof(TupleStore));
	return PoolAllocate(storePool);
}


static IOSignature TupleStoreGetCanonicalIOSignature(TupleStore const * store, IOSignature readerSignature)
{
	IOSignature canonicalSignature = {.parameterIO = {0}};
	for(index8 j = 0; j < store->nColumns; j++) {
		canonicalSignature.parameterIO[store->indexColumns[j]] = readerSignature.parameterIO[j];
	}
	return canonicalSignature;
}


static void createOperatorAndService(TupleStore const * store, RelationReaderSpec * readerSpec)
{
	Operator * op = CreateMachineOperator(
		store->nColumns, store->indexColumns, readerSpec, store->storage);

	IOSignature serviceIOSignature = TupleStoreGetCanonicalIOSignature(store, readerSpec->ioSignature);
	CreateService(
		(Service) {.relation = store->relation, .ioSignature = serviceIOSignature},
		op
	);
}


TupleStore * CreateTupleStore(Relation relation, StorageProvider const * provider, size8 nColumns, index8 const indexColumns[])
{
	TupleStore * store = allocateTupleStore();
	store->relation = relation;
	store->nColumns = nColumns;
	store->provider = provider;
	AcquireRelation(relation);
	RelationAttachTupleStore(relation, store);

	// setup index column array
	if(indexColumns)
		CopyMemory(indexColumns, store->indexColumns, store->nColumns);
	else {
		// use the identity order
		for(index8 i = 0; i < nColumns; i++)
			store->indexColumns[i] = i;
	}

	// Invalidate compiled services for this term form,
	// and mark corresponding relations "stale" if services were invalidated.
	// NOTE: it is not sufficient to invalidate only the current relation,
	// since any service compiled from a rule containing this term form
	// may now become dependent on the this relation.
	InvalidateTermFormServices(relation.termForm, INVALIDATE_BY_PRIMITIVE);
	// Call the storage provider to setup the relation implementation
	// and determine the number of readers
	size32 nReaders;
	store->storage = provider->setupStorage(nColumns, &nReaders);
	// Setup readers. Here we call the provider to fill out a readerSpec,
	// then we hand it over to machine operator.
	RelationReaderSpec readerSpec;
	for(index32 i = 0; i < nReaders; i++) {
		readerSpec = (RelationReaderSpec) {0};
		provider->setupReader(&readerSpec, i, store->storage);
		// setup the MACHINE operator and primitive service
		createOperatorAndService(store, &readerSpec);
	}
	return store;
}


void DropTupleStore(TupleStore * store)
{
	// If the relation table has storage, it must be empty
	ASSERT(!TupleStoreIsWritable(store) || (TupleStoreNTuples(store) == 0))
	RelationDetachTupleStore(store->relation, store);
	ReleaseRelation(store->relation);
	store->provider->free(store->storage);
	PoolFreeItem(storePool, store);
}


bool TupleStoreIsWritable(TupleStore const * store)
{
	return store->provider->addTuple;
}


byte TupleStoreAddTuple(TupleStore * store, Atom const tuple[], uint8 idPosition)
{
	ASSERT(TupleStoreIsWritable(store))
	// Permute tuple to the provider's order
	Atom providerTuple[store->nColumns];
	for(index8 i = 0; i < store->nColumns; i++)
		providerTuple[i] = tuple[store->indexColumns[i]];
	index8 providerIdPositon = idPosition ? store->indexColumns[idPosition - 1] + 1 : 0;
	// Call the provider to store the tuple
	byte result = store->provider->addTuple(store->storage, providerTuple, providerIdPositon);
	// Acquire atoms
	if(result == TUPLE_ADDED) {
		for(index8 i = 0; i < store->nColumns; i++) {
			if(i + 1 != idPosition)
				AcquireAtom(tuple[i], store->relation.typeSignature.atomTypes[i]);
		}
	}
	return result;
}


byte TupleStoreRemoveTuple(TupleStore * store, Atom const tuple[], uint8 idPosition)
{
	ASSERT(TupleStoreIsWritable(store))
	// Permute tuple to the provider's order
	Atom providerTuple[store->nColumns];
	for(index8 i = 0; i < store->nColumns; i++)
		providerTuple[i] = tuple[store->indexColumns[i]];
	index8 providerIdPositon = idPosition ? store->indexColumns[idPosition - 1] + 1 : 0;
	// Call the provider to remove th tuple
	byte result = store->provider->removeTuple(store->storage, providerTuple, providerIdPositon);

	// Release atoms
	if(result == TUPLE_REMOVED) {
		for(index32 i = 0; i < store->nColumns; i++) {
			if((i + 1) != idPosition)
				ReleaseTypedAtom(CreateTypedAtom(store->relation.typeSignature.atomTypes[i], tuple[i]));
		}
	}
	return result;
}


bool TupleStoreIsFinite(TupleStore const * store)
{
	// The relation is finite if the numberOfTuple() function is provided
	return store->provider->numberOfTuples;
}


size32 TupleStoreNTuples(TupleStore const * store)
{
	return store->provider->numberOfTuples(store->storage);
}

