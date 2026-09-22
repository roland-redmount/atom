/**
 * A framework for A service provider to create a read-only RelatonImplementation.
 * See for example library/math.c
 *
 * A service is registered from its signature, written in the notation a service
 * prints in, so that the relation, its column types and the parameter IO of the
 * service are all read off one string. Argument indexing is handled automatically.
 * 
 * NOTE: Some limitations of this framework:
 *
 * 1) Readers must be written referring to arguments by index, which is somewhat
 *    unintuitive.
 * 2) Each call to a machine service requires permuting the arguments from canonical
 *    to user order and back again, according to the stored argument index array,
 *    which can hurt performance for large calculations. A function yielding several
 *    tuples pays this per tuple rather than once.
 *
 * I think the only way to go around these would be to create a separate library
 * specification language where we can write something like
 * 
 * MATHFUNCTION add1 "+ x<INT + y<INT = z>INT" { z = x + y; return true}
 * 
 * which we could compile to C source, computing the canonical argument indices
 * at compile time, to yield
 * 
 * static bool add1(Atom arguments[])
 * {
 *   arguments[2]._int = arguments[0]._int + arguments[1]._int;
 *   return true;
 * }
 * 
 * where the argument[i] indices are now canonical order, and no runtime permutation
 * is needed.
 */

#ifndef MACHINE_SERVICE_H
#define MACHINE_SERVICE_H

#include "kernel/ServiceRegistry.h"

/**
 * Request a module ID identifying a group of relations created by the same author.
 * Modules are distinct from providers: a module can used multiple providers,
 * and a provider can be shared over multiple modules.
 * The returned ID is always nonzero.
 */
uint32 RequestModuleID(void);

/**
 * Create a stateless machine Operator with the given call function, and register a Service
 * based on the signature syntax string. The signature is a term whose actors
 * are all parameters. For example, the service that adds two integers has signature
 *
 *   "+ @1<INT + @2<INT = @3>INT"
 *
 * The signature must number its parameter @1, ... , @n in the order the MachineFunction
 * expects its arguments.
 * If the corresponding Relation does not exist, it is created, with the default provider (no storage).
 * Returns the registered service.
 * 
 * NOTE: unlike CreateTupleStore(), this does not invalidate compiled services,
 * and does not mark the relation stale. This function must not be called if there
 * already are compiled services associated with the relation.
 */
Service RegisterMachineService(
	uint32 moduleID, char const * signature,
	bool (*call)(void * state, Atom arguments[], void * readerData, void * storage));


Service RegisterMachineServiceWithState(
	uint32 moduleID, char const * signature, size32 stateSize,
	void (*setupState)(void * state, Atom arguments[], void * readerData, void * storage),
	bool (*call)(void * state, Atom arguments[], void * readerData, void * storage),
	void (*finalizeState)(void * state, void * readerData, void * storage));

/**
 * Remove all relations registered by a specific module.
 */
void FreeModuleRelations(uint32 moduleID);


#endif	// MACHINE_SERVICE_H
