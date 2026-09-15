/**
 * Interface for a relation implementations.
 *
 * This interface is designed to have minimal interactions with the kernel structures,
 * so that implementations don't have to make assumptions about kernel functions.
 *
 * This is analogous to RelationReader for operators.
 */

#ifndef STORAGEPROVIDER_H
#define STORAGEPROVIDER_H

#include "kernel/Parameter.h"

/**
 * This struct contains the data and functions specifying a relation reader.
 * This struct is filled out by providers to create a reader.
 */
typedef struct s_RelationReaderSpec
{
	IOSignature ioSignature;
	void * readerData;					// any reader-specific data
	size32 stateSize;

	/**
	 * Initialize the machine operator's state data, such as an iterator structure.
	 * This pointer may be 0 if the state needs no initialization.
	 * The state data is always cleared before calling this function.
	 * The operatorData pointer is the one given to CreateMachineOperator().
	 * The arguments are given in provider order.
	 */
	void (*setupState)(void * state, Atom arguments[], void * readerData, void * storage);

	/**
	 * Call (resume) an executing operator, return true if a tuple was produced,
	 * false if evaluation terminated. The call() function must write its results
	 * to the arguments tuple. The arguments are given in provider order.
	 * For an operator with no state, call() is only invoked once
	 * An operator with state is can be called repeatedly, until it returns false.
	 */
	bool (*call)(void * state, Atom arguments[], void * readerData, void * storage);

	/**
	 * Finalize the machine operator's state data.
	 * This pointer may be 0 if no finalization is required.
	 */
	void (*finalizeState)(void * state, void * readerData, void * storage);

	/**
	 * Deallocate any data associated with the reader. Called when the relation is being deallocate.
	 * This pointer may be 0 if no finalization is required.
	 * 
	 * NOTE: does this need the storage pointer? The function must not deallocate storage!
	 */
	void (*finalizeReader)(void * readerData, void * storage);

} RelationReaderSpec;

struct s_RelationImpl;

/**
 * This structure is managed by the kernel.
 */
typedef struct s_RelationReader
{
	RelationReaderSpec spec;
	struct s_RelationImpl * impl;		// provides access to the underlying storage
	struct s_RelationReader * next;		// linked list pointer
} RelationReader;


/**
 * A storage provider is a particular data storage type, such as a B-tree or array.
 * It specifies methods for creating storage, and optionally for writing tuples.
 * Some implementations may be read-only but still require storage, such as precalculated
 * values used to speed computed relations.
 * 
 * One provider may provide the storage of many relations, sharing the same callbacks.
 * The storage for each relation is described by a RelationImpl struct.
 *
 * For relations that do not require storage, the default storage provider can be used.
 * 
 * TODO: we should probably have a registry of storage providers, keyed by ID
 */
typedef struct s_StorageProvider {
	/**
	 * Setup the implementation for a new relation. This must allocate and initialize
	 * any necessary storage, and return a pointer to it. If no storage is required,
	 * this function should do nothing and return 0. The nReaders field must be set
	 * to the number of readers the providers needs to construct.
	 */
	void * (*setupStorage)(size8 nColumns, size32 * nReaders);

	/**
	 * Callback to setup a reader for the relation. This will be called multiple times
	 * with readerIndex = 0, 1, ..., nReader-1, where nReaders is the value determined
	 * by setupStorage(). The storage pointer is the one returned by setupStorage.
	 */
	void (*setupReader)(RelationReaderSpec * spec, index32 readerIndex, void * storage);

	/**
	 * Add a tuple to the relation.
	 * The atom types are fixed, so providing an Atom array is sufficient.
	 * If idPosition is > 0 it indicates the 1-based position of an identified
	 * atom (the tuple is part of an ifact).
	 * May be 0 if the provider does not support write access.
	 */
	byte (*addTuple)(void * storage, Atom const tuple[], uint8 idPosition);

	/**
	 * Remove a specific tuple from storage.
	 * If the stored tuple had an identified atom, it must match the given idPosition,
	 * or an error occurs.
	 * May be 0 if the provider does not support write access.
	 */
	byte (*removeTuple)(void * storage, Atom const tuple[], uint8 idPosition);

	/**
	 * Return number of tuples in the relation table
	 * NOTE: should perhaps be moved? Not a write operation
	 * May be 0 if the relation is not enumerable (infinite).
	 */
	size32 (*numberOfTuples)(void * storage);

	/**
	 * Free the storage provider's data.
	 */
	void (*free)(void * storage);

} StorageProvider;

// result codes for StorageProvider.addTuple()
#define TUPLE_ADDED			1
#define TUPLE_EXISTS		2

// result codes for StorageProvider.removeTuple()
#define TUPLE_REMOVED		1
#define TUPLE_NOT_FOUND		2
#define TUPLE_PROTECTED		3

/**
 * The default provider. This can only be used for read-only (non-mutable) relations.
 */
extern StorageProvider defaultProvider;


/**
 * Describes the implementation and storage for a specific relation.
 * Every relation must specify this struct.
 */
typedef struct s_RelationImpl {
	StorageProvider * provider;
	size8 nColumns;
	size32 nReaders;
	void * storage;
	RelationReader * firstReader;		// linked list of readers
} RelationImpl;

/**
 * Stable allocation of RelationImpl structs
 * TODO: make static ?
 */
RelationImpl * AllocateRelationImpl(void);

/**
 * Stable allocation of RelationReader structs
 * TODO: make static ?
 */
RelationReader * AllocateRelationReader(void);

void FreeRelationReader(RelationReader const * reader);

/**
 * Create a new relation implementation with the given storage provider.
 */
RelationImpl * CreateRelationImpl(StorageProvider const * provider, size8 nColumns);

void FreeRelationImpl(RelationImpl const * impl);

void RelationImplAddTuple(RelationImpl * impl, Atom const tuple[], uint8 idPosition);

void RelationImplRemoveTuple(RelationImpl * impl, Atom const tuple[], uint8 idPosition);

/**
 * Add a reader to the given implementation.
 */
void RelationImplAddReader(RelationImpl * impl, RelationReader * reader);



#endif	// STORAGEPROVIDER_H
