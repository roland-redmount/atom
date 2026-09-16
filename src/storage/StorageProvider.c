
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


RelationReader * RelationImplAddReader(RelationImpl * impl, RelationReaderSpec const * readerSpec)
{
	RelationReader * reader = AllocateRelationReader();
	reader->impl = impl;
	reader->spec = *readerSpec;		// store a copy

	// Append to the list of readers
	RelationReader ** readerSlot = &(impl->firstReader);
	while(*readerSlot)
		readerSlot = &((*readerSlot)->next);
	*readerSlot = reader;

	impl->nReaders++;
	return reader;
}


RelationImpl * CreateRelationImpl(StorageProvider const * provider, size8 nColumns)
{
	ASSERT(provider)
	RelationImpl * impl = AllocateRelationImpl();
	impl->provider = provider;
	impl->storage = provider->setupStorage(nColumns, &(impl->nReaders));
	// Setup readers
	RelationReader ** readerSlot = &(impl->firstReader);
	for(index32 i = 0; i < impl->nReaders; i++) {
		*readerSlot = AllocateRelationReader();
		(*readerSlot)->impl = impl;
		provider->setupReader(&((*readerSlot)->spec), i, impl->storage);
		readerSlot = &((*readerSlot)->next);
	}
	return impl;
}


byte RelationImplAddTuple(RelationImpl * impl, Atom const tuple[], uint8 idPosition)
{
	return impl->provider->addTuple(impl->storage, tuple, idPosition);
}


byte RelationImplRemoveTuple(RelationImpl * impl, Atom const tuple[], uint8 idPosition)
{
	return impl->provider->removeTuple(impl->storage, tuple, idPosition);
}


bool RelationImplIsWritable(RelationImpl * impl)
{
	// The relation is writable if the addTuple() function is provided
	return impl->provider->addTuple;
}


bool RelationImplIsEnumerable(RelationImpl * impl)
{
	// The relation is enumerable if the numberOfTuple() function is provided
	return impl->provider->numberOfTuples;
}


size32 RelationImplNRows(RelationImpl * impl)
{
	return impl->provider->numberOfTuples(impl->storage);
}


void FreeRelationImpl(RelationImpl const * impl)
{
	// free readers
	RelationReader * reader = impl->firstReader;
	while(reader) {
		if(reader->spec.finalizeReader)
			reader->spec.finalizeReader(reader->spec.readerData, impl->storage);
		RelationReader * tmp = reader;
		reader = reader->next;
		PoolFreeItem(readerPool, tmp);
	}
	impl->provider->free(impl->storage);
	PoolFreeItem(relationImplPool, impl);
}
