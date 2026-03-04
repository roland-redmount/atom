/**
 * The service registry maps signatures to relation tables
 * or bytecode services- For bytecode services, a signature
 * is a formula where the actors contain parameters.
 * For relation tables, any arguments are assumed to be valid,
 * and the signature's actor list is null.
 * 
 * Currently, it is not possible to register both a relation table
 * and a bytecode service of the same form. This restriction may
 * be relaxed in the future, but this would require merging results
 * across multiple services.
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
 * Create a new relation table for a form. Returns the created table,
 * or 0 if a table with the same form already existed.
 * 
 * NOTE: The registry does not acquire the form atom, and does not make
 * use of it in any way except as a key.
 * 
 * TODO: This should probably not create a btree, just add an existing one?
 */
BTree * RegistryCreateTable(Atom form);

/**
 * Return the relation table for a form, or 0 if none exists.
 * The returned pointer is guaranteed to be valid for as long as
 * the relation table exists.
 */
BTree * RegistryLookupTable(Atom form);

/**
 * Remove a relation table from the registry.
 * All facts must have been retracted so that relation table is empty.
 */
void RegistryRemoveTable(Atom form);

/**
 * Add a bytecode service to the registry
 */
Service RegistryAddBytecodeService(Atom bytecode);

/**
 * Remove a bytecode service from the registry.
 */
void RegistryRemoveBytecodeService(Atom form);



/**
 * Find a service matching a given form.
 * 
 * TODO: there will be multiple matches, this should be an iterator
 */
Service RegistryFindService(Atom form);


#endif  // TABLEREGISTRY_H
