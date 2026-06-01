/**
 * A service is a procedure that can evaluate queries of a particular
 * form. The service registry maps signatures to expression that can be
 * evaluated by the interpreter. Dispatch uses the registry to
 * match services to queries.
 * 
 * For B-tree services are automatically removed by RetractFact() when
 * the last tuple in the relation table is removed. 
 *
 * TODO: AT_SERVICE atoms are currently not reference counted, but we should
 * keep track of services that appear in Expression leaves; in this case we
 * must not remove the child service before the "parent". Hence, we do need
 * reference counting. We could prevent "garbage collection" of services by
 * always keeping 1 reference to the AT_SERVICE atom in the stored ServiceRecord.
 */

#ifndef SERVICEREGISTRY_H
#define SERVICEREGISTRY_H

#include "kernel/expression.h"
#include "kernel/machineservice.h"
#include "kernel/RelationBTree.h"


enum ServiceType {
	SERVICE_NONE,
	SERVICE_EXPRESSION,
	SERVICE_MACHINE
};


/**
 * A service is represented by an AT_SERVICE atom, which is a special hash
 * of the service's signature, which consists of a form and a parameter list.
 * The parameters list contains DT_PARAMETER atoms (see Parameter.h),
 * indicating the io mode (in/out) and atom type for each parameter.
 * 
 * A service s subsumes another service t iff (1) the forms are equal, and
 * (2) there exists a valid form permutation such that, for each parameter p
 * of s and corresponding parameter q of t: (i) their io modes are equal,
 * or the io mode of p is in/out; and (ii) their datum types are requal, or
 * the datum type of p is AT_NONE. 
 * TODO: If service s subsumes service t, only one of them may be in the registry. 
 * 
 * NOTE: provided service records are fixed size, we could store them in a pool.
 * This would allow us to directly refer to services by pointers and perhaps
 * get rid of the AT_SERVICE atom type.
 */
typedef struct s_ServiceRecord {
	// service hash value
	Atom service;
	// we store the form and parameters lists of the signature separately
	// to allow iterating across all services matching a given form
	Atom form;
	Atom parameters;
	Expression expression;
} ServiceRecord;


/**
 * Create a service record ID atom from a formula consisting
 * of a form and a parameter list
 */
Atom CreateServiceRecordID(Atom form, Atom parameters);

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
 * Core services are created during bootstrap.
 * They are accessible using RegistryGetServiceRecord() like all
 * other services, but can also be retrieved with an integer index
 * corresponding to the core predicate indices in kernel.h
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
 */
ServiceRecord RegistryGetCoreServiceRecord(index32 index);

/**
 * Get the relation table corresponding to a core predicate.
 */
BTree * RegistryGetCoreBTreeService(index32 index);

/**
 * Remove all core services.
 */
void RegistryTeardownCoreServices(void);

/**
 * Add service to the registry.
 * Returns an AT_SERVICE atom.
 */
Atom RegistryAddService(Atom form,  Atom parameters, Expression const * expression);

/**
 * Convenience function add a B-tree machine service the registry,
 * generating a list of untyped parameters. Returns an AT_SERVICE atom.
 */
Atom RegistryAddBTreeService(Atom form, BTree * btree);

/**
 * Remove the given service from the registry.
 */
void RegistryRemoveService(Atom service);

/**
 * Retrieve the service record for the given service atom (AT_SERVICE).
 * For matching services to queries, see dispatch.c
 */
ServiceRecord RegistryGetServiceRecord(Atom service);


/**
 * Iterating over services
 */
typedef struct {
	ServiceRecord keyRecord;
	BTreeIterator btreeIterator;
} RegistryIterator;

/**
 * Create iterator over services matching the given form.
 */
void RegistryIterate(Atom form, RegistryIterator * iterator);

bool RegistryIteratorNext(RegistryIterator * iterator);

ServiceRecord RegistryIteratorGetService(RegistryIterator * iterator);

void RegistryIteratorEnd(RegistryIterator * iterator);

/**
 * Retrieve the service with the given form and parameters list.
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
void PrintService(ServiceRecord const * service);

void RegistryDump(void);


#endif  // SERVICEREGISTRY_H
