/**
 * Registering a machine service, which is a relation computed by a C function
 * rather than stored in a table. This is how a library provides a primitive
 * such as addition; see library/math.c
 *
 * A service is registered from its signature, written in the notation a service
 * prints in, so that the relation, its column types and the parameter IO of the
 * service are all read off one string. See RegisterMachineService()
 */

#ifndef MACHINE_SERVICE_H
#define MACHINE_SERVICE_H

#include "kernel/ServiceRegistry.h"


/**
 * Most arguments a machine service may have. A machine function is given its
 * arguments in a context buffer of this fixed size, since MachineProvider.contextSize
 * is a property of the provider rather than of one operator.
 */
#define MACHINE_SERVICE_MAX_ARITY	8


/**
 * A machine function computes one tuple of a relation from the arguments the
 * caller has bound. It returns true if it produced a tuple.
 *
 * The arguments are in the order the signature numbers them, so the argument
 * written @1 is arguments[0]. This is not the order of the relation's columns,
 * which is the canonical role order of its form; RegisterMachineService() permutes
 * between the two, so a function can use the positions it was written with.
 *
 * A function returns false when it computes nothing for the arguments it was given.
 * That is what lets a partial function, such as a division that rejects a zero
 * divisor, and a test, such as (even 4), be written as machine services.
 */
typedef bool (*MachineFunction)(Atom arguments[]);


/**
 * Register a machine service evaluated by the given function, and create its relation
 * if this is the first service registered for it.
 *
 * The signature is a term whose actors are parameters, each written as a number, an
 * io direction and an atom type. The service that adds two integers is
 *
 *   "+ @1<INT + @2<INT = @3>INT"
 *
 * and the one that subtracts, by solving the same equation for the other unknown, is
 *
 *   "+ @1<INT + @2>INT = @3<INT"
 *
 * Both name the same relation (+ + =), so the second call finds the relation the first
 * created. A signature must number its arguments 1 to the arity, each exactly once.
 *
 * A negated predicate is a relation of its own, so a service for one is registered by
 * writing its signature negated, as in "! even @1<INT".
 *
 * Returns the registered service, whose parameterIO array is owned by the service
 * registry; see ServiceRegistryAdd()
 */
Service RegisterMachineService(char const * signature, MachineFunction function);


/**
 * Remove every service registered by RegisterMachineService(), and the relations
 * created for them. Called when tearing down the libraries, before KernelShutdown()
 */
void FreeMachineServices(void);


#endif	// MACHINE_SERVICE_H
