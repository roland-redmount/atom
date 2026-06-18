/**
 * The service registry maps signatures (form, parameters) to services.
 * Dispatch uses the registry to match services to queries.
 */

#ifndef SERVICEREGISTRY_H
#define SERVICEREGISTRY_H

#include "kernel/service.h"
#include "kernel/RelationBTree.h"


/**
 * A service record identified by a signature consisting of a form and an
 * array of AT_PARAMETER atoms indicating the io mode (in/out) and atom type
 * for each parameter.
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
	Atom * parameters;
	Service const * service;
} ServiceRecord;

void ServiceRecordGetAtomTypes(ServiceRecord const * record, byte * atomTypes);

/**
 * Compare service record based on the service atom field.
 * Two ServiceRecord compare equal if (1) both forms and parameters match, or
 * (2) forms match and serviceOrKey is 0.
 * 
 * TODO: This does not allow having PARAMETER_IN_OUT subsume other parameters;
 * we now simply compare the parameter lists for equality. So the only cases
 * we can represent is (1) no in/out parameters or (2) all in/out parameters (parameters = 0)
 * For more complex cases, we will need to iterate over matching forms and
 * check for conflicts when adding new services.
 */
int8 CompareServiceRecords(ServiceRecord const * record, ServiceRecord const * recordOrKey);

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
void RegistryAddService(Atom predicateForm, Atom const * parameters, Service const * service);

/**
 * Convenience function add a B-tree machine service the registry,
 * generating a list of untyped in/out parameters.
 */
void RegistryAddBTreeService(Atom form, RelationBTree * tree);

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
 * Retrieve the service record with the given form and parameters array
 * of the same length as the form arity.
 * If a matching service does not exist, returns a zero record.
 */
ServiceRecord RegistryFindService(Atom form, Atom const * parameters);

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
