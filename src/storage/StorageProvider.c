
#include "storage/StorageProvider.h"
#include "memory/pool.h"

// A simple provider ID mechanism. We simply hand out increasing numbers
// as provider IDs.
// static uint32 nextProviderID = 1;


// uint32 RequestProviderID(void)
// {
// 	return nextProviderID++;
// }



void * relationImplPool = 0;

RelationImpl * AllocateRelationImpl(void)
{
	if(!relationImplPool)
		relationImplPool = CreatePool(sizeof(RelationImpl));
	return PoolAllocate(relationImplPool);
}


void * readerPool = 0;

RelationReader * AllocateRelationReader(void)
{
	if(!readerPool)
		readerPool = CreatePool(sizeof(RelationReader));
	return PoolAllocate(readerPool);
}

void FreeRelationReader(RelationReader const * reader)
{
	PoolFreeItem(readerPool, reader);
}


/**
 * The default storage provider
 */
static void * defaultCreateImpl(size8 nColumns, size32 * nReaders)
{
	return 0;
}

static void defaultFree(void * storage)
{
}

StorageProvider defaultProvider = {
	.setupStorage = defaultCreateImpl,
	.free = defaultFree,
};


void RelationImplAddReader(RelationImpl * impl, RelationReader * reader)
{
	RelationReader ** readerSlot = &(impl->firstReader);
	while(*readerSlot)
		readerSlot = &((*readerSlot)->next);
	*readerSlot = reader;
	impl->nReaders++;
}


RelationImpl * CreateRelationImpl(StorageProvider const * provider, size8 nColumns)
{
	RelationImpl * impl = AllocateRelationImpl();
	impl->storage = provider->setupStorage(nColumns, &(impl->nReaders));
	// Setup readers
	RelationReader ** readerSlot = &(impl->firstReader);
	for(index32 i = 0; i < impl->nReaders; i++) {
		*readerSlot = AllocateRelationReader();
		provider->setupReader(&((*readerSlot)->spec), i, impl->storage);
		readerSlot = &((*readerSlot)->next);
	}
	return impl;
}


void FreeRelationImpl(RelationImpl const * impl)
{
	impl->provider->free(impl->storage);
	PoolFreeItem(relationImplPool, impl);
}
