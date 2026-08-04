/**
 * The service registry keeps track of the services available for each
 * registered relation. Dispatch uses the registry to match queries to services.
 * The relations themselves are registered separately; see RelationRegistry.h
 */

#ifndef SERVICE_REGISTRY_H
#define SERVICE_REGISTRY_H

#include "btree/btree.h"
#include "kernel/service.h"
#include "kernel/RelationTable.h"


/**
 * Setup an empty service registry. Called during bootstrapping only.
 */
void SetupServiceRegistry(void);

/**
 * Associated a service with a relation in the registry.
 * Aquires a reference to the service.
 */
void RelationAddService(RelationTable const * relation, byte const parameterIO[], Service * service);

/**
 * Dissociate the given service from a relation in the registry.
 * Releases a reference to the service.
 */
void RelationRemoveService(RelationTable const * relation, Service * service);

/**
 * Dissociate all services from the a relation in the registry.
 */
void RelationRemoveAllServices(RelationTable const * relation);

/**
 * Deallocate the registry. Before calling this function,
 * all services must have been removed.
 */
void FreeServiceRegistry(void);

/**
 * Total number of registered services.
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
Service * RegistryFindService(RelationTable const * relation, byte const parameterIO[]);


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


#endif  // SERVICE_REGISTRY_H
