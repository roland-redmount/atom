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
enum ServiceKind {
	// Provided by the kernel, a stored relation or a machine service, and so part of
	// the knowledge base rather than derived from it
	SERVICE_PRIMITIVE = 1,
	// Compiled from the rules by the compiler. A compiled service is a cache over the
	// knowledge base, and is removed again when a change could alter what it yields;
	// see ServiceRegistryInvalidateTermForm().
	SERVICE_COMPILED = 2,
	// Registered by the compiler for the duration of one compilation, so that a
	// recursive term has something to dispatch to, and removed again when that
	// compilation finishes; see registerTemporaryServices() in compiler.c. A temporary
	// service is scaffolding rather than an answer: nothing outlives the compilation to
	// depend on it, and nothing invalidates it.
	SERVICE_TEMPORARY = 3,
};

typedef struct s_Service {
	RelationTable const * relation;
	byte * parameterIO;
	// Pointer to the root of the operator tree defining this service.
	// NOTE: cannot be const * if we want to do AcquireOperator(op).
	// NOTE: not named "operator", which is a reserved word in C++
	Operator * op;
	enum ServiceKind kind;
} Service;

/**
 * Setup an empty service registry. Called during bootstrapping only.
 */
void SetupServiceRegistry(void);

/**
 * Associates an operator with a relation in the service registry, giving a service.
 * Acquires a reference to the operator. Returns a copy of the created service.
 *
 * NOTE: For services whose form contain repeated roles, such as `(a b b)`,
 * the signature must be unique under form permutation: for example, the two services
 * `(a 1<INT b 2<INT c 3>FLOAT)` and `(a 1<INT b 2>FLOAT c 3<INT)` have the same signature
 * under the permutation $(1, 3, 2)$  and so cannot co-exist. There is currently no check
 * for this criterion, as it would require form permutation which causes bootstrap problems.
 * 
 * DEBUG builds assert this where it would take effect, in
 * DispatchIteratorNext().
 *
 * A compiled service records which services its operator tree is built on, so that it
 * can be invalidated when one of them changes; see ServiceRegistryInvalidateTermForm().
 * Registering a primitive service is itself such a change, as a query of its term form
 * may now have one more relation to match, and so invalidates the compiled services of
 * that form.
 */
Service ServiceRegistryAdd(
	RelationTable const * relation, byte const parameterIO[], Operator * op,
	enum ServiceKind kind);

/**
 * Dissociate the service of the given operator from a relation in the service
 * registry. Releases the reference to the operator. The relation itself is left to the
 * caller, which owns it.
 *
 * Every compiled service built on the removed one is invalidated, and so are the ones
 * built on those; see ServiceRegistryInvalidateTermForm().
 */
void ServiceRegistryRemove(RelationTable const * relation, Operator * op);

/**
 * Dissociate all services from the given relation in the service registry, invalidating
 * the compiled services built on them; see ServiceRegistryRemove().
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
 * Number of registered services compiled from the rules; see ServiceKind.
 */
size32 ServiceRegistryNCompiled(void);


/**
 * Invalidate compiled services depending on the given term form.
 * Removes every compiled service whose signature matches the given form, or that
 * calls a service of that form, transitively. A compiled service answers a query as the
 * rules and the facts stood when it was compiled, so a change to either has to remove the
 * services it could affect; the next query then compiles them again. Removing too much
 * costs a compilation, removing too little gives a wrong answer.
 *
 * Any relation associated with a removed service is also removed if it ends up with no
 * services left and has no provider (no storage).
 *
 * NOTE: this modifies the registries, so it cannot run while a query is being read: an
 * open DispatchIterator or MixedTypeRelation write-locks against modification.
 */
void ServiceRegistryInvalidateByTermForm(Atom termForm);

/**
 * Remove every compiled service, whatever its form; see ServiceRegistryInvalidateTermForm().
 */
void ServiceRegistryInvalidateAll(void);


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
