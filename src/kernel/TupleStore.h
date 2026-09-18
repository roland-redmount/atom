
#ifndef TUPLE_STORE_H
#define TUPLE_STORE_H

#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "storage/StorageProvider.h"


// NOTE: this is now just a linked list contained, use util/LinkedList.h ?
typedef struct s_RelationReader
{
	RelationReaderSpec spec;
	struct s_RelationReader * next;		// linked list pointer
} RelationReader;


typedef struct s_TupleStore {
	Relation relation;
	/*
	 * The order of index columns. The stored tuples will be ordered lexicographically by
	 * indexColumns[0], ..., indexColumns[nColumns-1]. Hence, lookup should be fast when
	 * leading columns are specified in this order, while out-of-order
	 * columns may lead to table scanning.
	 * For example, a relation with canonical order (element list position) and
	 * indexColumns = {1, 2, 0} will be ordered first by list, then by position, then by element;
	 * queries (@list _ _) and (@list @position _) should be fast, but (_ _ @element) may be slow.
	 */
	index8 indexColumns[RELATION_MAX_ARITY];
	size8 nColumns;

	StorageProvider const * provider;
	void * storage;

	size32 nReaders;
	RelationReader * firstReader;		// linked list of readers

} TupleStore;

/**
 * Create a TupleStore using the given storage provider. Creates storage and/or services
 * as specified by the provider. The relation must not already exist.
 */
TupleStore * CreateTupleStore(
	Relation relation, StorageProvider const * provider, size8 nColumns, index8 const indexColumns[]);

void DropTupleStore(TupleStore * store);

/**
 * Add a RelationReader to the TupleStore and register the corresponding MACHINE operator and Service.
 */
Service TupleStoreAddReader(TupleStore * store, RelationReaderSpec const * readerSpec);

bool TupleStoreIsWritable(TupleStore const * store);

byte TupleStoreAddTuple(TupleStore * store, Atom const tuple[], uint8 idPosition);

byte TupleStoreRemoveTuple(TupleStore * store, Atom const tuple[], uint8 idPosition);

bool TupleStoreIsEnumerable(TupleStore const * store);

size32 TupleStoreNTuples(TupleStore const * store);


#endif		// TUPLE_STORE_H
