/**
 * The service registry maps signatures (form, parameters) to services.
 * Dispatch uses the registry to match services to queries.
 */

#ifndef SERVICEREGISTRY_H
#define SERVICEREGISTRY_H

#include "kernel/service.h"
#include "kernel/RelationBTree.h"


/**
 * A service record is identified by a signature consisting of a form and an
 * array of AT_PARAMETER atoms indicating the io mode (in/out) and atom type
 * for each parameter.
 * 
 * NOTE: the form is currently always a predicate form, which means we
 * cannot have services for negated predicates like (! odd x). 
 * It's not clear to me yet if this is a major limitation.
 */
typedef struct s_ServiceRecord {
	Atom form;
	Atom * parameters;
	Service * service;	// cannot be const * if we want to do AcquireService(service)
} ServiceRecord;

// void ServiceRecordGetAtomTypes(ServiceRecord const * record, byte * atomTypes);

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
 * Total number of service records.
 */
size32 RegistryNServices(void);

/**
 * Core services are created during bootstrap.
 * They are accessible by iteration but can also be retrieved
 * with an integer index corresponding to the core predicate indices in kernel.h
 */

/**
* Create a core B-tree service during bootstrap.
*/
// void RegistryAddCoreBTreeService(index32 index, Atom form, RelationBTree * btree);

/**
 * Must be called during bootstrap, after all core services have been installed
 * and we are able to create parameter lists.
 */
// void RegistryFinalizeCoreServices(void);

/**
 * Get the service record corresponding to a core predicate.
 * The index is the form index used by kernel.h
 * 
 * NOTE: this is not longer valid as predicate forms -- tables are not 1:1
 */
// ServiceRecord const * RegistryGetCoreServiceRecord(index32 index);

/**
 * Get the relation B-tree corresponding to a core predicate.
 */
// RelationBTree * RegistryGetCoreBTreeService(index32 index);

/**
 * Remove all core services.
 */
void RegistryTeardownCoreServices(void);

/**
 * Add a service to the registry.
 * NOTE: currently this method will ASSERT(false) if the (form, parameters) pair already
 * exists in the registry.
 */
void RegistryAddService(Atom predicateForm, Atom const * parameters, Service const * service);

/**
 * Convenience function add a B-tree machine service the registry,
 * generating a list of untyped in/out parameters.
 */
// void RegistryAddBTreeService(Atom form, RelationBTree * tree);

/**
 * Remove the given service record (key) from the registry.
 * The removed record is written to the given service record.
 */
void RegistryRemoveService(ServiceRecord * record);


/**
 * Iterating over services
 */
typedef struct {
	ServiceRecord keyRecord;
	BTreeIterator btreeIterator;
} RegistryIterator;

/**
 * Create iterator over all service records matching the given form.
 */
void RegistryIterate(Atom form, RegistryIterator * iterator);

bool RegistryIteratorNext(RegistryIterator * iterator);

ServiceRecord const * RegistryIteratorPeekService(RegistryIterator * iterator);

void RegistryIteratorEnd(RegistryIterator * iterator);

/**
 * Retrieve the service for the given form and parameters array,
 * which must be the same length as the form arity.
 * If a matching service does not exist, returns 0
 */
Service const * RegistryFindService(Atom form, Atom const * parameters);

/**
 * Retrieve the service of the given form with all parameters in/out untyped.
 * If a matching service does not exist, returns a zero record.
 * 
 * NOTE: remove this, untyped no longer allowed
 */
// ServiceRecord RegistryFindUntypedService(Atom form);


/**
 * For debugging
 */
void PrintServiceRecord(ServiceRecord const * record);

void RegistryDump(void);


#endif  // SERVICEREGISTRY_H
