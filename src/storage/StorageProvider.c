
#include "memory/allocator.h"
#include "memory/paging.h"
#include "memory/references.h"
#include "storage/RelationBTree.h"
#include "storage/StorageProvider.h"


void SetupStorageProviders(void)
{
	StorageProvider * providers = Allocate(N_STORAGE_PROVIDERS * sizeof(StorageProvider));
	SetPersistentState(STATE_KEY_STORAGE_PROVIDERS, providers);
	// PROVIDER_DEFAULT has no functions, and is left cleared by Allocate()
	RelationBTreeSetupProvider(&(providers[PROVIDER_BTREE]));
}


void FreeStorageProviders(void)
{
	StorageProvider * providers = GetPersistentState(STATE_KEY_STORAGE_PROVIDERS);
	ClearReferences(providers, N_STORAGE_PROVIDERS * sizeof(StorageProvider));
	Free(providers);
	SetPersistentState(STATE_KEY_STORAGE_PROVIDERS, 0);
}


StorageProvider const * GetStorageProvider(StorageProviderId providerId)
{
	ASSERT(providerId < N_STORAGE_PROVIDERS)
	StorageProvider const * providers = GetPersistentState(STATE_KEY_STORAGE_PROVIDERS);
	ASSERT(providers)
	return &(providers[providerId]);
}
