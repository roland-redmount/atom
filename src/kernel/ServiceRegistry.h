/**
 * The registry keep track of available relations and their services.
 * Dispatch uses the registry to match queries to services.
 */

#ifndef REGISTRY_H
#define REGISTRY_H

#include "btree/btree.h"
#include "kernel/service.h"
#include "kernel/RelationTable.h"


/**
 * Setup an empty registry. Called during bootstrapping only.
 */
void SetupRegistry(void);

/**
 * Add a new relation table to the registry. For computed relations, provider is 0;
 * when non zero, the provider pointer must remain valid for as
 * long as the relation is in use. Calls provider->createTable().
 */
RelationTable const * CreateRelationTable(
	RelationTableProvider * provider, Atom form, size8 nColumns, byte const atomTypes[]);

/**
 * Remove a relation table, include all stored tuples (if any), and all associated services.
 */
void RemoveRelationTable(RelationTable const * relation);

/**
 * Add a service to a relation.
 */
void RelationAddService(RelationTable const * relation, byte const parameterIO[], Service * service);

/**
 * Remove the given service from a relation
 */
void RelationRemoveService(RelationTable const * relation, Service * service);

/**
 * Locate a relation table for given (form, column types).
 */
RelationTable const * FindRelationTable(Atom form, size8 nColumns, byte const atomTypes[]);

/**
 * Deallocate the registry. Before calling this function,
 * all relation tables and services must have been removed.
 */
void FreeRegistry(void);

/**
 * Total number of service records.
 */
size32 RegistryNServices(void);

/**
 * A service record links a relation to a service with a particular parameter IO
 */
typedef struct s_ServiceRecord {
	RelationTable const * relation;
	byte * parameterIO;
	Service * service;	// cannot be const * if we want to do AcquireService(service)
} ServiceRecord;


/**
 * Iterating over services
 */
typedef struct {
	RelationTable const * table;
	BTreeIterator btreeIterator;
} RegistryIterator;

/**
 * Create iterator over all service records for a given relation table
 */
void RegistryIterate(RelationTable const * table, RegistryIterator * iterator);

bool RegistryIteratorNext(RegistryIterator * iterator);

ServiceRecord const * RegistryIteratorPeekRecord(RegistryIterator const * iterator);

void RegistryIteratorEnd(RegistryIterator * iterator);

/**
 * Retrieve the service for the given form and parameters array,
 * which must be the same length as the form arity.
 * If a matching service does not exist, returns 0
 */
Service const * RegistryFindService(RelationTable const * relation, byte const parameterIO[]);


/**
 * For debugging
 */
void PrintServiceRecord(ServiceRecord const * record);

/**
 * Dump all tuples in a the given relation table.
 * Requires an associated service for enumerating all tuples.
 */
void RelationTableDump(RelationTable const * table);

/**
 * Print a list of all registered services
 */
void RegistryDumpServices(void);


#endif  // REGISTRY_H
