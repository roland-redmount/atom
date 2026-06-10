/**
 * The service registry maps signatures (form, parameters) to services.
 * Dispatch uses the registry to match services to queries.
 * 
 * TODO: Service records are currently not reference counted, but we should
 * keep track of services that appear in Expression leaves; in this case we
 * must not remove the child service before the "parent". Hence, we do need
 * some form of reference counting.
 */

#ifndef SERVICEREGISTRY_H
#define SERVICEREGISTRY_H

#include "kernel/service.h"
#include "kernel/RelationBTree.h"


/**
 * A service record identified by a signature consisting of a form and a parameter
 * list. The parameters list contains AT_PARAMETER atoms (see Parameter.h),
 * indicating the io mode (in/out) and atom type for each parameter.
 * 
 * NOTE: the form is currently always a predicate form, which means we
 * cannot have services for negated predicates like (! odd x). 
 * It's not clear to me yet if this is a major limitation.
 * 
 * A signature s subsumes another signature t iff (1) the forms are equal, and
 * (2) there exists a valid form permutation such that, for each parameter p
 * of s and corresponding parameter q of t: (i) their io modes are equal,
 * or the io mode of p is in/out; and (ii) their datum types are equal, or
 * the datum type of p is AT_NONE. 
 * 
 * TODO: If service s subsumes service t, only one of them may be in the registry. 
 */
typedef struct s_ServiceRecord {
	Atom form;
	Atom parameters;
	Service * service;
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
void RegistryAddCoreBTreeService(index32 index, Atom form, BTree * btree);

/**
 * Must be called during bootstrap, after all core services have been installed
 * and we are able to create parameter lists.
 */
void RegistryFinalizeCoreServices(void);

/**
 * Get the service record corresponding to a core predicate.
 * The index is the form index used by kernel.h
 */
ServiceRecord const * RegistryGetCoreServiceRecord(index32 index);

/**
 * Get the relation table corresponding to a core predicate.
 */
BTree * RegistryGetCoreBTreeService(index32 index);

/**
 * Remove all core services.
 */
void RegistryTeardownCoreServices(void);

/**
 * Add a service to the registry.
 * NOTE: currently this method will ASSERT(false) if the (form, parameters) pair already
 * exists in the registry.
 */
void RegistryAddService(ServiceRecord const * record);

/**
 * Convenience function add a B-tree machine service the registry,
 * generating a list of untyped parameters.
 */
void RegistryAddBTreeService(Atom form, BTree * btree);

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
 * Retrieve the service record with the given form and parameters list.
 * If a matching service does not exist, returns a zero record.
 */
ServiceRecord RegistryFindService(Atom form, Atom parameters);

/**
 * Retrieve the service of the given form with all parameters in/out untyped.
 * If a matching service does not exist, returns a zero record.
 */
ServiceRecord RegistryFindUntypedService(Atom form);


/**
 * For debugging
 */
void PrintServiceRecord(ServiceRecord const * service);

void RegistryDump(void);


#endif  // SERVICEREGISTRY_H
