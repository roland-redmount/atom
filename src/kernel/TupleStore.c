
#include "kernel/ClosedRelation.h"
#include "kernel/dictionary.h"
#include "kernel/operator.h"
#include "kernel/TupleStore.h"
#include "lang/FormPermutation.h"
#include "memory/paging.h"
#include "memory/pool.h"

// These are defined in TypedRelation.c
extern void RelationAttachTupleStore(TypedRelation relation, TupleStore * store);
void RelationDetachTupleStore(TypedRelation relation, TupleStore * store);


/**
 * Pool allocation for structures
 * Stored in persistent state STATE_KEY_TUPLE_STORES
 */
static void * storePool = 0;


void InitializeTupleStores(void)
{
	ASSERT(GetPersistentState(STATE_KEY_TUPLE_STORES) == 0)
	storePool = CreatePool(sizeof(TupleStore));
	SetPersistentState(STATE_KEY_TUPLE_STORES, storePool);
}


void RestoreTupleStores(void)
{
	storePool = GetPersistentState(STATE_KEY_TUPLE_STORES);
	ASSERT(storePool)
}


void FreeTupleStores(void)
{
	ASSERT(PoolNItems(storePool) == 0)
	FreePool(storePool);
	SetPersistentState(STATE_KEY_TUPLE_STORES, 0);
	storePool = 0;
}


static TupleStore * allocateTupleStore(void)
{
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


TupleStore * CreateTupleStore(TypedRelation relation, StorageProvider const * provider, size8 nColumns, index8 const indexColumns[])
{
	// CLAUDE: The tuples of the opposite relation of a closed relation are inferred; see CloseRelation()
	ASSERT(!RelationIsClosedInferred(relation.form))
	TupleStore * store = allocateTupleStore();
	store->relation = relation;
	store->nColumns = nColumns;
	store->provider = provider;
	AcquireRelation(relation);
	RelationAttachTupleStore(relation, store);

	bool primitivesStale = InvalidateServicesForNewRelation(relation.form);

	size32 nReaders = 0;
	if(provider->setupStorage) {
		// Call the storage provider to setup the relation implementation
		// and determine the number of readers
		store->storage = provider->setupStorage(nColumns, &nReaders);
	}
	if(nReaders > 0) {
		// setup index column array
		if(indexColumns)
			CopyMemory(indexColumns, store->indexColumns, store->nColumns);
		else {
			// use the identity order
			for(index8 i = 0; i < nColumns; i++)
				store->indexColumns[i] = i;
		}
		store->hasIndexOrder = true;

		// Setup readers. Here we call the provider to fill out a readerSpec,
		// then we hand it over to machine operator.
		RelationReaderSpec readerSpec;
		for(index32 i = 0; i < nReaders; i++) {
			readerSpec = (RelationReaderSpec) {0};
			provider->setupReader(&readerSpec, i, store->storage);
			// create the MACHINE operator and primitive service
			Operator * op = CreateMachineOperator(
				store->nColumns, store->indexColumns, &readerSpec, store->storage);
			IOSignature serviceIOSignature = TupleStoreGetCanonicalIOSignature(
				store, readerSpec.ioSignature);
			Service service = {.relation = store->relation, .ioSignature = serviceIOSignature};
			CreateService(service, op);
			if(primitivesStale)
				ServiceMarkStale(service);
		}
	}
	return store;
}


void DropTupleStore(TupleStore * store)
{
	// If the relation table has storage, it must be empty
	ASSERT(!TupleStoreIsWritable(store) || (TupleStoreNTuples(store) == 0))
	RelationDetachTupleStore(store->relation, store);
	ReleaseRelation(store->relation);
	if(store->provider->free)
		store->provider->free(store->storage);
	PoolFreeItem(storePool, store);
}


bool TupleStoreIsWritable(TupleStore const * store)
{
	return store->provider->addTuple;
}


/* CLAUDE: Write the tuple, with its columns permuted by the given permutation, in the
   provider's column order; see TupleStore.indexColumns. */
static void getProviderTuple(
	TupleStore const * store, Atom const tuple[], index8 const permutation[], Atom providerTuple[])
{
	for(index8 i = 0; i < store->nColumns; i++)
		providerTuple[i] = tuple[permutation[store->indexColumns[i]]];
}


byte TupleStoreAddTuple(TupleStore * store, Atom const tuple[])
{
	ASSERT(TupleStoreIsWritable(store))

	// We store one tuple for each form permutation to support querying for
	// any role permutation of a term.
	FormIterator * formIterator = CreateFormIterator(store->relation.form);
	index8 permutation[store->nColumns];
	Atom providerTuple[store->nColumns];
	// The first permutation is the identity
	GetTuplePermutation(formIterator, permutation);
	getProviderTuple(store, tuple, permutation, providerTuple);
	// Call the provider to store the tuple
	byte result = store->provider->addTuple(store->storage, providerTuple);
	if(result == TUPLE_ADDED) {
		// CLAUDE: Store the tuple in every column order that is the same fact, so that
		// a reader finds the fact whichever order dispatch passes the query arguments in;
		// see permutationMatch() in dispatch.c. A permuted tuple equal to one already
		// stored, from actors that are equal, yields TUPLE_EXISTS.
		while(NextFormPermutation(formIterator)) {
			GetTuplePermutation(formIterator, permutation);
			getProviderTuple(store, tuple, permutation, providerTuple);
			store->provider->addTuple(store->storage, providerTuple);
		}
		// Acquire atoms
		for(index8 i = 0; i < store->nColumns; i++)
			AcquireAtom(tuple[i], store->relation.typeSignature.atomTypes[i]);
	}
	FreeFormIterator(formIterator);
	return result;
}


byte TupleStoreRemoveTuple(TupleStore * store, Atom const tuple[])
{
	ASSERT(TupleStoreIsWritable(store))
	// Remove all permutations of the tuple
	FormIterator * formIterator = CreateFormIterator(store->relation.form);
	index8 permutation[store->nColumns];
	Atom providerTuple[store->nColumns];
	GetTuplePermutation(formIterator, permutation);
	getProviderTuple(store, tuple, permutation, providerTuple);
	// Call the provider to remove th tuple
	byte result = store->provider->removeTuple(store->storage, providerTuple);

	if(result == TUPLE_REMOVED) {
		// CLAUDE: Remove the column orders stored by TupleStoreAddTuple()
		while(NextFormPermutation(formIterator)) {
			GetTuplePermutation(formIterator, permutation);
			getProviderTuple(store, tuple, permutation, providerTuple);
			store->provider->removeTuple(store->storage, providerTuple);
		}
		// Release atoms
		for(index32 i = 0; i < store->nColumns; i++)
			ReleaseTypedAtom(CreateTypedAtom(store->relation.typeSignature.atomTypes[i], tuple[i]));
	}
	FreeFormIterator(formIterator);
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

