
#ifndef TUPLE_STORE_H
#define TUPLE_STORE_H

#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "storage/StorageProvider.h"


typedef struct s_TupleStore {
	Relation relation;
	/*
	 * The order of index columns. The stored tuples will be ordered lexicographically by
	 * indexColumns[0], ..., indexColumns[nColumns-1]. Hence, lookup should be fast when
	 * leading columns are specified in this order, while out-of-order
	 * columns may lead to table scanning.
	 * 
	 * For example, a relation with canonical order (element list position) and
	 * indexColumns = {1, 2, 0} will be ordered first by list, then by position, then by element;
	 * queries (@list _ _) and (@list @position _) should be fast, but (_ _ @element) may be slow.
	 * 
	 * indexOrder is set by CreateTupleStore(). The underlying storage provider is not
	 * aware of indexOrder, and always works with index order {0, 1, 2, ... }
	 */
	index8 indexColumns[RELATION_MAX_ARITY];
	size8 nColumns;

	StorageProvider const * provider;
	void * storage;

} TupleStore;

/**
 * Create a TupleStore using the given storage provider. Creates storage and/or services
 * as specified by the provider. The relation must not already exist.
 */
TupleStore * CreateTupleStore(
	Relation relation, StorageProvider const * provider, size8 nColumns, index8 const indexColumns[]);

/**
 * Remove the given TupleStore.
 * Does not deallocate readers: they must be removed before calling this function.
 */
void DropTupleStore(TupleStore * store);

/**
 * Add a RelationReader to the TupleStore and register the corresponding MACHINE operator and Service.
 */
// Service TupleStoreAddReader(TupleStore * store, RelationReaderSpec const * readerSpec);

/**
 * Generate the canonical IOSignature from the IO signature of a RelationReader
 * acting on this tuple store, by permuting w.r.t. the store's indexOrder.
 */
// IOSignature TupleStoreGetCanonicalIOSignature(TupleStore * store, IOSignature readerSignature);

bool TupleStoreIsWritable(TupleStore const * store);

byte TupleStoreAddTuple(TupleStore * store, Atom const tuple[], uint8 idPosition);

byte TupleStoreRemoveTuple(TupleStore * store, Atom const tuple[], uint8 idPosition);

bool TupleStoreIsEnumerable(TupleStore const * store);

size32 TupleStoreNTuples(TupleStore const * store);


#endif		// TUPLE_STORE_H
