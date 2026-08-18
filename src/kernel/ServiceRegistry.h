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
 * NOTE: the Service structure is stored in the registry, and is copied by value
 * since we cannot return pointers into the B-tree storage structure (they may
 * change over time). The registry holds a reference to Service.operator.
 */
typedef struct s_Service {
	RelationTable const * relation;
	byte * parameterIO;
	// Pointer to the root of the operator tree defining this service.
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
 *
 * The services of one relation have distinct parameter IO arrays, up to a permutation of
 * the predicate form preserving the column atom types. Two services related by such a
 * permutation are one service with its parameters renamed: they enumerate the same
 * relation under the same bindings, and one query would then match both and receive
 * every tuple twice. DEBUG builds assert this where it would take effect, in
 * DispatchIteratorNext().
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
 * Create iterator over all services for a given relation table
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
void PrintService(Service const * service);

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
