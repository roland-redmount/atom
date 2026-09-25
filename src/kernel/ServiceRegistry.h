/**
 * The service registry keeps track of the Services registered for each Relation.
 * Dispatch uses the service registry to match queries to services.
 */

#ifndef SERVICE_REGISTRY_H
#define SERVICE_REGISTRY_H

#include "btree/btree.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"


/**
 * A Service is defined by an (Relation, IOSignature, EqualitySignature) tuple.
 * A service is implemented by an Operator graph, and holds a pointer to the root
 * operator of this graph. 
 * The operator takes as many arguments as there are unique parameters, and the EqualitySignature
 * indicates columns that map to the same parameter. For example, the service
 * (edge @1<ID from @2<ID to @2>ID) has 3 parameters but only 2 unique parameters,
 * and its root operator then takes 2 arguments; its EqualitySignature is {0, 0, 2},
 * where each element is 0 if the parameter occurs for the first time at that position,
 * or else the 1-based position of the first occurence. This format allows us to encode
 * a service with no repeated arguments as the zero vector. See EqualitySignatureGetArgumentMap()
 * 
 * Because an Operator assumes a specific IOSignature, the pair (Relation, Operator)
 * is also 1:1 with a Service.
 */
typedef struct s_Service {
	Relation relation;
	IOSignature ioSignature;
	EqualitySignature equalitySignature;
} Service;

bool SameServices(Service service1, Service service2);


typedef struct s_ServiceRecord {
	Service service;
	Operator * op;		// The root operator of the operator graph for this service
	bool isStale;
} ServiceRecord;


/**
 * Setup an empty service registry. Called during bootstrapping only.
 */
void SetupServiceRegistry(void);

/**
 * Register a new Service with the given Operator. Attaches the services' Relation
 * to the Operator and acquires the Relation.
 * If the service is compiled, there must not exist a service already.
 * 
 * NOTE: For services whose form contain repeated roles, such as `(a b b)`,
 * the signature must be unique under form permutation: for example, the two services
 * `(a 1<INT b 2<INT c 3>FLOAT)` and `(a 1<INT b 2>FLOAT c 3<INT)` have the same signature
 * under the permutation $(1, 3, 2)$  and so cannot co-exist. There is currently no check
 * for this criterion, as it would require form permutation which causes bootstrap problems.
 * DEBUG builds assert this constraint in DispatchIteratorNext().
 */
void CreateService(Service service, Operator * op);

/**
 * Remove the service identified by the given operator and relation.
 * The operator cannot be a MACHINE operator.
 * This removes the services' operator, and recursively removes all operators
 * and services that depend on it. Return the total number of services removed.
 */
size32 RemoveService(Service service);

/**
 * Remove all services for the given relation.
 */
void ServiceRegistryRemoveAll(Relation relation);

/**
 * Deallocate the service registry. Before calling this function,
 * all services must have been removed.
 */
void FreeServiceRegistry(void);

/**
 * Total number of registered services.
 */
size32 NumberOfServices(void);

/**
 * Number of registered services compiled from rules.
 */
size32 NumberOfCompiledServices(void);

/**
 * Mark a primitive service as "stale", so that next query matching it
 * will trigger compilation. 
 */
void ServiceMarkStale(Service service);


void ServiceMarkNotStale(Service service);


bool ServiceIsStale(Service service);


/**
 * Invalidate compiled services for the given term form. 
 * This function removes (1) every compiled Service for each Relation matching the given form,
 * and (2) every compiled Service that calls any services matching the term form, transitively.
 * Also marks the involved relations as stale; exactly when a relation become stale differs
 * depending on useCase. Returns the total number of services removed.
 * 
 * NOTE: this modifies the registries, so it cannot run while a query is being read: an
 * open DispatchIterator or MixedTypeRelation write-locks against modification.
 */
typedef enum e_InvalidationUseCase
{
	INVALIDATE_BY_RULE = 1,				// invalidate due to adding/removing a rule
	INVALIDATE_BY_PRIMITIVE = 2,		// invalidate due to adding primitive services
} InvalidationUseCase;

size32 InvalidateTermFormServices(Atom termForm, InvalidationUseCase useCase);

/**
 * Remove all compiled services from the registry.
 */
void RemoveAllCompiledServices(void);


/**
 * Iterating over services
 */
typedef struct {
	Relation relation;
	BTreeIterator btreeIterator;
} ServiceIterator;

/**
 * Create iterator over all service records for a given relation
 */
void ServiceRegistryIterate(Relation relation, ServiceIterator * iterator);

bool ServiceIteratorNext(ServiceIterator * iterator);

ServiceRecord const * ServiceIteratorPeekRecord(ServiceIterator const * iterator);

void ServiceIteratorEnd(ServiceIterator * iterator);

/**
 * Retrieve the operator of the service for the given Service.
 * If the service is not registers, returns 0
 */
Operator * ServiceGetOperator(Service service);

/**
 * View the service record for the service. The returned pointer is
 * valid only as long as the service registery is not altered.
 */
ServiceRecord const * ServiceGetRecord(Service service);

/**
 * For debugging
 */
void PrintService(Service service);

/**
 * Dump all tuples of the given relation.
 * Requires an associated service for enumerating all tuples.
 */
void RelationDump(Relation relation);

/**
 * Print a list of all registered services
 */
void ServiceRegistryDump(void);


#endif  // SERVICE_REGISTRY_H
