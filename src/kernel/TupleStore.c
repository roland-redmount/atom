
#include "kernel/operator.h"
#include "kernel/TupleStore.h"
#include "memory/pool.h"

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


void * readerPool = 0;

RelationReader * AllocateRelationReader(void)
{
	if(!readerPool)
		readerPool = CreatePool(sizeof(RelationReader));
	return PoolAllocate(readerPool);
}

// defined in ServiceRegistry.c
extern Service CreatePrimitiveService(Relation relation, IOSignature ioSignature, Operator * op);

static Service createOperatorAndService(TupleStore const * store, RelationReaderSpec * readerSpec)
{
	Operator * op = CreateMachineOperator(
		store->nColumns, store->indexColumns, readerSpec, store->storage);

	// We must permute the reader IOSignature to match the Service order
	IOSignature serviceIOSignature = {.parameterIO = {0}};
	for(index8 j = 0; j < store->nColumns; j++) {
		serviceIOSignature.parameterIO[store->indexColumns[j]] = readerSpec->ioSignature.parameterIO[j];
	}
	return CreatePrimitiveService(store->relation, serviceIOSignature, op);
}


// This is defined in Relation.c
extern void RelationSetTupleStore(Relation relation, TupleStore * store);

TupleStore * CreateTupleStore(Relation relation, StorageProvider const * provider, size8 nColumns, index8 const indexColumns[])
{
	TupleStore * store = allocateTupleStore();
	store->relation = relation;
	store->nColumns = nColumns;
	store->provider = provider;
	AcquireRelation(relation);
	RelationSetTupleStore(relation, store);

	// setup index column array
	if(indexColumns)
		CopyMemory(indexColumns, store->indexColumns, store->nColumns);
	else {
		// use the identity order
		for(index8 i = 0; i < nColumns; i++)
			store->indexColumns[i] = i;
	}

	// Call the storage provider to setup the relation implementation and its readers
	store->storage = provider->setupStorage(nColumns, &(store->nReaders));
	// Setup readers
	RelationReader ** readerSlot = &(store->firstReader);
	for(index32 i = 0; i < store->nReaders; i++) {
		*readerSlot = AllocateRelationReader();
		provider->setupReader(&((*readerSlot)->spec), i, store->storage);
		// setup the MACHINE operator and primitive service
		createOperatorAndService(store, &((*readerSlot)->spec));
		readerSlot = &((*readerSlot)->next);
	}
	return store;
}


void DropTupleStore(TupleStore * store)
{
	// If the relation table has storage, it must be empty
	ASSERT(!TupleStoreIsWritable(store) || (TupleStoreNTuples(store) == 0))

	// free readers
	RelationReader * reader = store->firstReader;
	while(reader) {
		if(reader->spec.finalizeReader)
			reader->spec.finalizeReader(reader->spec.readerData, store->storage);
		RelationReader * tmp = reader;
		reader = reader->next;
		PoolFreeItem(readerPool, tmp);
	}
	store->provider->free(store->storage);
	PoolFreeItem(storePool, store);
}


Service TupleStoreAddReader(TupleStore * store, RelationReaderSpec const * readerSpec)
{
	RelationReader * reader = AllocateRelationReader();
	reader->spec = *readerSpec;		// store a copy

	// Append to the list of readers
	RelationReader ** readerSlot = &(store->firstReader);
	while(*readerSlot)
		readerSlot = &((*readerSlot)->next);
	*readerSlot = reader;
	store->nReaders++;

	return createOperatorAndService(store, &(reader->spec));
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


bool TupleStoreIsEnumerable(TupleStore const * store)
{
	// The relation is enumerable if the numberOfTuple() function is provided
	return store->provider->numberOfTuples;
}


size32 TupleStoreNTuples(TupleStore const * store)
{
	return store->provider->numberOfTuples(store->storage);
}

