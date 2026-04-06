/**
 * The service registry maps signatures to relation tables
 * or bytecode services- For bytecode services, a signature
 * is a formula where the actors contain parameters.
 * For relation tables, any arguments are assumed to be valid,
 * and the signature's actor list is null.
 * 
 * For simplicity, currently, we only store one service for each form,
 * either bytecode or table. There cannot be multiple bytecode
 * services with different parameters (in/out).
 */

#ifndef TABLEREGISTRY_H
#define TABLEREGISTRY_H

#include "kernel/RelationBTree.h"

/**
 * A service might be a bytecode service, a table representation,
 * or a hardcoded C function.
 */

enum ServiceType {
	SERVICE_NONE = 0,
	SERVICE_FUNCTION ,
	SERVICE_BYTECODE,
	SERVICE_BTREE,
};

typedef struct s_ServiceInfo {
	Atom form;
	Atom parameters;
	enum ServiceType type;
	union {
		BTree * tree;
		Atom bytecode;
		void (*function)(void);		// TODO
	} service;
	
} Service;


/**
 * Setup an empty service registry. Called during bootstrapping only.
 */
void SetupRegistry(void);

/**
 * Deallocate the registry. Before calling this function, all facts
 * must have been retracted so that relation tables are empty.
 */
void TeardownRegistry(void);

/**
 * Total number of services registered.
 */
size32 RegistryNServices(void);

/**
 * Get the relation table corresponding to a core predicate.
 */
BTree * RegistryGetCoreTable(index32 index);

/**
* A number of tables in the registry must be hardcoded to support
* core language elements. These must be created when a new "world" is initialized.
* At this time, we cannot call FormArity(), so the arity must be specified.
*/
void RegistryCreateCoreTable(index32 index, Atom form, size8 arity);

/**
 * Add a B-tree backed relation table to the registry.
 */
Service RegistryAddBTreeService(Atom form, BTree * btree);

/**
 * Add a bytecode service to the registry
 */
Service RegistryAddBytecodeService(Atom bytecode);

/**
 * Remove a service from the registry.
 * 
 * If the service is a relation table, all facts must have been retracted
 * so that table is empty. The B-tree will be deallocated.
 */
void RegistryRemoveService(Atom form);

/**
 * Find the service registered for a given form.
 * If no service is found, the returned service.type equals SERVICE_NONE
 */
Service RegistryFindService(Atom form);


#endif  // TABLEREGISTRY_H
