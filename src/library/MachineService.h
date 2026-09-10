/**
 * A simple framework for service providers to regiser a machine service.
 * See for example library/math.c
 *
 * A service is registered from its signature, written in the notation a service
 * prints in, so that the relation, its column types and the parameter IO of the
 * service are all read off one string. Argument indexing is handled automatically.
 * 
 * NOTE: Some limitations of this framework:
 *
 * 1) Functions must be written referring to arguments by index, which is somewhat
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
 * Allows a machine service provider to request an ID. This ID will be associated
 * with services registered by RegisterMachineService(), so that they can later 
 * be found and removed by FreeMachineServices(). The retunrs ID is always nonzero.
 */
uint32 RequestProviderID(void);

/**
 * Create a machine Operator based on the given operatorSpec, and register a Service
 * based on the signature syntax string. The signature is a term whose actors
 * are all parameters. For example, the service that adds two integers has signature
 *
 *   "+ @1<INT + @2<INT = @3>INT"
 *
 * A signature must number its arguments 1 ... arity, in the order the MachineFunction
 * expects its arguments in the arguments[] array.
 * Returns the registered service.
 */

Service RegisterMachineService(char const * signature, MachineOperatorSpec operatorSpec);

/**
 * Remove all services registered by RegisterMachineService() for a given providerID.
 */
void FreeMachineServices(uint32 providerID);


#endif	// MACHINE_SERVICE_H
