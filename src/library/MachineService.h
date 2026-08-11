/**
 * Registering a machine service, which is a relation computed by a C function
 * rather than stored in a table. This is how a library provides a primitive
 * such as addition; see library/math.c
 *
 * A service is registered from its signature, written in the notation a service
 * prints in, so that the relation, its column types and the parameter IO of the
 * service are all read off one string. See RegisterMachineService()
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
 * Most arguments a machine service may have. A machine function is given its arguments
 * in a context buffer of this fixed size, which keeps the context a plain structure with
 * one flexible member, that being the state. The buffer could be sized to the arity of
 * the service instead, should one ever need more arguments than this.
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
 *
 * A function yielding several tuples is called repeatedly until it returns false. The
 * function keeps its state in a block whose size is specified by RegisterMachineService().
 * The state is zeroed before the first call. isFirstCall is true on the first call.
 * A function of registered with no state can yield at most one tuple, and is
 * given a null state pointer.
 */
typedef bool (*MachineFunction)(Atom arguments[], void * state, bool isFirstCall);


/**
 * Register a machine service evaluated by the given function, and create its relation
 * if this is the first service registered for it. The signature is a term whose actors
 * are all parameters. For example, the service that adds two integers has signature
 *
 *   "+ @1<INT + @2<INT = @3>INT"
 *
 * and the one that subtracts, by solving the same equation for the other unknown, is
 *
 *   "+ @1<INT + @2>INT = @3<INT"
 *
 * Both name the same relation (+ + =), so the second call finds the relation the first
 * created. A signature must number its arguments 1 ... arity, consecutively.
 *
 * A state area of size = stateSize will be allocated for stateful service; for a
 * function computing a single tuple (stateless), set stateSize = 0. A stateful service
 * may yield several tuples, and its index order must match the numbering of output parameters.
 * Since inputs are constant over one evaluation, their order does not matter.
 * For example, the range iterator
 *
 *   "lower @1<INT number @2>INT upper @3<INT"
 *
 * yields tuples differing only in @2, which ascends, so the inputs @1 and @3 can be in
 * any order. See the ordering contract in kernel/operator.h.
 *
 * Returns the registered service, whose parameterIO array is owned by the service
 * registry; see ServiceRegistryAdd()
 */
Service RegisterMachineService(
	char const * signature, MachineFunction function, size32 stateSize);


/**
 * Remove every service registered by RegisterMachineService(), and the relations
 * created for them. Called when tearing down the libraries, before KernelShutdown()
 */
void FreeMachineServices(void);


#endif	// MACHINE_SERVICE_H
