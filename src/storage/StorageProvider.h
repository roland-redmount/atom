/**
 * Interface for a relation storage provider, such as RelationBTree.
 * One provider may provide the storage of many relations, sharing the same callbacks.
 * Providers live under src/storage/, 
 *
 * This interface is designed to have minimal interactions with the kernel structures,
 * so that storage implementations don't have to make assumptions about kernel functions.
 */

#ifndef STORAGEPROVIDER_H
#define STORAGEPROVIDER_H

#include "kernel/Parameter.h"
#include "kernel/operator.h"


typedef void (*CreateServiceCallback)(
	void * table, MachineOperatorProvider * operatorProvider,
	void * providerData, size32 contextSize, IOSignature ioSignature
);

typedef struct s_StorageProvider {

	/**
	 * Create storage for a new relation table. The provided callback must be called
	 * by the service provider to register primitive services.
	 * Required services:
	 * 1) The all-input service is required for contradiction checking by AssertFact()
	 * 2) The all-output service that enumerates every tuple is required by RelationDump(),
	 *    and in order to generate FILTER services (table scanning)
	 *
	 * The returned storage data pointer is assigned to the RelationTable.storage field.
	 */
	void * (*createStorage)(
		index8 const * indexColumns, size8 nColumns, void * table, CreateServiceCallback callback);

	/**
	 * Add a tuple to storage.
	 * The atom types are fixed, so providing an Atom array is sufficient.
	 * If idPosition is > 0 it indicates the 1-based position of an identified
	 * atom (the tuple is part of an ifact).
	 */
	byte (*addTuple)(void * storage, Atom const tuple[], uint8 idPosition);

	/**
	 * Remove a specific tuple from storage.
	 * If the stored tuple had an identified atom, it must match the given idPosition,
	 * or an error occurs.
	 */
	byte (*removeTuple)(void * storage, Atom const tuple[], uint8 idPosition);

	/**
	 * Return number of tuples in the relation table
	 */
	size32 (*numberOfTuples)(void * storage);

	/**
	 * Free the storage provider's data.
	 */
	void (*free)(void * storage);

} StorageProvider;

#endif	// STORAGEPROVIDER_H
