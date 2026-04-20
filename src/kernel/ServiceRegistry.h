/**
 * The service registry maps signatures to services, so that dispatch
 * can efficiently match services to queries. A service can either
 * be a bytecode service or a machine code (C) service.
 * 
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
 * For B-tree services are automatically removed by RetractFact() when
 * the last tuple in the relation table is removed. 
 *
 * TODO: AT_SERVICE atoms are currently not reference counted. It makes sense
 * that Bytecode services should be removed manually, as removal amounts to
 * retracting all facts that are provided by the service. BUT we must also
 * know when a service depends on a calling "child" service, in which case
 * the bytecode will store its AT_SERVICE atom as a constant; in this case we
 * must not remove the child service before the "parent". Hence, we do need
 * reference counting. We could prevent "garbage collection" of services by
 * always keeping 1 reference to the AT_SERVICE atom in the stored ServiceRecord.
 */

#ifndef SERVICEREGISTRY_H
#define SERVICEREGISTRY_H

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
* Core services are created during bootstrap when a new "world" is initialized.
* Core services are accessible using RegistryGetServiceRecord() &c like all
* other services, can also be retrieved with an integer index. 
 */

/**
* Create a core B-tree service. During bootstrap we cannot call FormArity()
* so the arity must be specified. Returns the created B-tree.
*/
BTree * RegistryAddCoreBTreeService(index32 index, Atom form, size8 arity);

/**
 * Must be called during bootstrap, after all core services have been installed
 * and we are able to create parameter lists.
 */
void RegistryFinalizeCoreServices(void);
/**
 * Get the relation table corresponding to a core predicate.
 */
BTree * RegistryGetCoreTable(index32 index);

/**
 * Remove all core services.
 */
void RegistryTeardownCoreServices(void);

/**
 * Add a B-tree backed service the registry.
 * The registry takes ownership of btree, will deallocate it.
 * Returns an AT_SERVICE atom.
 */
Atom RegistryAddBTreeService(Atom form, BTree * btree);

/**
 * Add a bytecode service to the registry.
 * Returns an AT_SERVICE atom.
 */
Atom RegistryAddBytecodeService(Atom form, Atom bytecode);

/**
 * Remove the given service from the registry.
 * 
 * If the service is a relation table, all facts must have been retracted
 * so that table is empty; the B-tree will be deallocated.
 */
void RegistryRemoveService(Atom service);

/**
 * Retrieve the service record for the given service atom (AT_SERVICE).
 * For matching services to queries, see dispatch.c
 */
ServiceRecord RegistryGetServiceRecord(Atom service);

/**
 * Special case for B-tree services, where parameters are always in/out untyped.
 * If a matching B-tree service does not exist, returns a zero record.
 * (Convenience function, keeping it for now)
 */
ServiceRecord RegistryFindBTreeService(Atom form);


/**
 * Iterating over services
 */
typedef struct {
	Atom form;
	BTreeIterator btreeIterator;
} RegistryIterator;

/**
 * Create iterator over services matching the given form.
 */
void RegistryIterate(Atom form, RegistryIterator * iterator);

bool RegistryIteratorHasService(RegistryIterator const * iterator);

ServiceRecord RegistryIteratorGetService(RegistryIterator * iterator);

void RegistryIteratorNext(RegistryIterator * iterator);

void RegistryIteratorEnd(RegistryIterator * iterator);




#endif  // SERVICEREGISTRY_H
