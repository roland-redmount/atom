/**
 * The service registry maps signatures to services, so that dispatch
 * can efficiently match services to queries. A service can either
 * be a bytecode service or a machine code (C) service.
 * A signature is a formula whose actors list contains DT_PARAMETER atoms,
 * indicating the parameter io mode (in/out) and atom type for each argument;
 * atom type may be AT_NONE to indicate that any type is acceptable. 
 * 
 * There can be only one service for each signature. A DT_NONE parameter
 * acts as a "wildcard" when determining equality between signatures,
 * matching any other parameter type: for example, the two signature
 * (foo x:INT bar y:FLOAT) and (foo x:INT bar y:NONE) cannot coexist,
 * since a query with a FLOAT atom y matches both.
 */

#ifndef TABLEREGISTRY_H
#define TABLEREGISTRY_H

#include "kernel/RelationBTree.h"

/**
 * TODO: for bytecode to be able to create contexts from services
 * (instrution CTX), this structure must be represented as an Atom.
 * Currently we use AT_BYTECODE but this must be generalized
 * to cover native services.
 */

enum ServiceType {
	SERVICE_NONE = 0,
	SERVICE_BYTECODE,
	SERVICE_BTREE,
};

typedef struct s_Service {
	Atom service;
	// we store the form and parameters lists of the signature separately
	// to allow iterating across all services matching a given form
	Atom form;
	// For parameters, a zero value is taken to mean a list where all
	// parameters have type AT_NONE and io mode PARAMETER_IN_OUT.
	// This solves a bootstrap problem where parameter lists cannot be
	// generated for core relation tables before (list) is available.
	Atom parameters;
	enum ServiceType type;
	union {
		BTree * tree;
		Atom bytecode;
	} provider;
} ServiceRecord;


/**
 * Setup an empty service registry. Called during bootstrapping only.
 */
void SetupRegistry(void);

/**
 * Deallocate the registry. Before calling this function,
 * all services must have been removed.
 */
void FreeRegistry(void);

/**
 * Total number of services registered.
 */
size32 RegistryNServices(void);

/**
 * Get the relation table corresponding to a core predicate.
 */
BTree * RegistryGetCoreTable(index32 index);

/**
* Create a special B-tree services in the registry for core language elements.
* These must be created during bootstrap when a new "world" is initialized.
* At this time, we cannot call FormArity(), so the arity must be specified.
*/
void RegistryAddCoreBTreeService(index32 index, Atom form, size8 arity);

/**
 * Remove a core service.
 */
void RegistryRemoveCoreBTreeService(index32 index);

/**
 * Add a B-tree backed service the registry.
 * The parameter field of the service record will be 0.
 * Returns an AT_SERVICE atom.
 */
Atom RegistryAddBTreeService(Atom form, BTree * btree);

/**
 * Add a bytecode service to the registry.
 * Returns an AT_SERVICE atom.
 */
Atom RegistryAddBytecodeService(Atom signature, Atom bytecode);

/**
 * Remove the given service from the registry.
 * 
 * If the service is a relation table, all facts must have been retracted
 * so that table is empty; the B-tree will be deallocated.
 */
void RegistryRemoveService(Atom service);

/**
 * Retrieve a service record.
 * If no service is found, the returned service.type equals SERVICE_NONE
 * For matching services to queries, see dispatch.c
 */
ServiceRecord RegistryGetServiceRecord(Atom service);

/**
 * Special case for B-tree services (for now)
 */
ServiceRecord RegistryFindBTreeService(Atom form);


/**
 * Iterating over services
 */
typedef struct {
	Atom form;
} RegistryIterator;

/**
 * Create iterator over services matching the given form,
 */
void RegistryIterate(Atom form, RegistryIterator * iterator);

ServiceRecord RegistryIteratorGetService(RegistryIterator * iterator);



#endif  // TABLEREGISTRY_H
