/**
 * The service registry keeps track of the services available for each
 * registered relation. Dispatch uses the registry to match queries to services.
 * The relations themselves are registered separately; see RelationRegistry.h
 */

#ifndef SERVICE_REGISTRY_H
#define SERVICE_REGISTRY_H

#include "btree/btree.h"
#include "kernel/operator.h"
#include "kernel/RelationTable.h"


/**
 * A service is a relation with a particular parameter IO, together with the
 * operator tree evaluating it: the analogue of a procedure in atom, and what
 * dispatch matches a query against. Most operators are internal nodes of such a
 * tree and have no signature of their own; see operator.h.
 *
 * NOTE: a service is a record naming an operator, not something to be created
 * and freed: it is copied by value, and the registry owns the reference to its
 * operator.
 */
typedef struct s_Service {
	RelationTable const * relation;
	byte * parameterIO;
	// NOTE: cannot be const * if we want to do AcquireOperator(op).
	// NOTE: not named "operator", which is a reserved word in C++
	Operator * op;
} Service;

/**
 * Setup an empty service registry. Called during bootstrapping only.
 */
void SetupServiceRegistry(void);

/**
 * Associates an operator with a relation in the service registry, giving a service.
 * Acquires a reference to the operator.
 * Returns a copy of the created service.
 */
Service ServiceRegistryAdd(RelationTable const * relation, byte const parameterIO[], Operator * op);

/**
 * Dissociate the service of the given operator from a relation in the service
 * registry. Releases the reference to the operator.
 */
void ServiceRegistryRemove(RelationTable const * relation, Operator * op);

/**
 * Dissociate all services from the given relation in the service registry.
 */
void ServiceRegistryRemoveAll(RelationTable const * relation);

/**
 * Deallocate the service registry. Before calling this function,
 * all services must have been removed.
 */
void FreeServiceRegistry(void);

/**
 * Total number of registered services.
 */
size32 ServiceRegistryCount(void);


/**
 * Iterating over services
 */
typedef struct {
	RelationTable const * table;
	BTreeIterator btreeIterator;
} ServiceIterator;

/**
 * Create iterator over all service records for a given relation table
 */
void ServiceRegistryIterate(RelationTable const * table, ServiceIterator * iterator);

bool ServiceIteratorNext(ServiceIterator * iterator);

Service const * ServiceIteratorPeekService(ServiceIterator const * iterator);

void ServiceIteratorEnd(ServiceIterator * iterator);

/**
 * Retrieve the operator of the service for the given relation and parameter IO
 * array, which must be the same length as the relation arity.
 * If a matching service does not exist, returns 0
 */
Operator * ServiceRegistryFind(RelationTable const * relation, byte const parameterIO[]);

/**
 * For debugging
 */
void PrintService(Service const * record);

/**
 * Dump all tuples in a the given relation table.
 * Requires an associated service for enumerating all tuples.
 */
void RelationTableDump(RelationTable const * table);

/**
 * Print a list of all registered services
 */
void ServiceRegistryDump(void);


#endif  // SERVICE_REGISTRY_H
