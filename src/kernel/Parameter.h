/**
 * An AT_PARAMETER atom is used in service signatures to mark a position
 * in the actor list where a parameter is a expected. Parameters are
 * different from variables: while a variable indicates "any atom"
 * and has no particular input/output direction, a parameter
 * indicates one specific atom, although its identity is unknown.
 * Parameters are identified within a formula by a unique number.
 * 
 * Output parameters may superfically seem similar to variables as we can
 * use variables in place of "outputs" in queries, but they are not
 * the same: a variable can during compilation be mapped to either an
 * input or output parameter, depending on the service signatures.
 */

#ifndef	PARAMETER_H
#define	PARAMETER_H 

#include "kernel/Relation.h"
#include "kernel/typedtuple.h"
#include "lang/TypedAtom.h"
#include "lang/Atom.h"

/**
 * Operator parameters can be input, output.
 * In syntax, we denote input parameters by @, output by $.
 * (Maybe < and > are more intuitive after all? Output feel
 *  like going right-to-left so should be >, consistent with
 *  Unix pipes.)
 * 
 * PARAMETER_IN_OUT can be used to represent input/output combinations
 * compactly for services that can act on either inputs or outputs,
 * such as table lookup with / without variables.
 */
#define PARAMETER_IN		1
#define PARAMETER_OUT		2


/**
 * The parameter IO of a service: the direction of each of its parameters, in relation
 * column order. This is what distinguishes the services of one relation from one another,
 * and together with the relation it is the key a service is registered under; see
 * ServiceRegistry.h. Analogous to the TypeSignature of a relation, and exists for the same
 * reason: to pass a fixed-size array by value.
 */
typedef struct s_IOSignature {
	byte parameterIO[RELATION_MAX_ARITY];
} IOSignature;

/**
 * The IO signature of the given parameter directions, zero filled beyond nColumns. For a
 * signature built from an array at hand.
 */
IOSignature CreateIOSignature(byte const parameterIO[], size8 nColumns);


/**
 * Generate an array of AT_PARAMETER atoms corresponding to the actors tuple,
 * such that each non-variable atom in the actors tuple yields an input parameter
 * of the same type as the atom, and each variable yields an output parameter,
 * whose type is unknown.
 * The actors tuple must not contain AT_PARAMETER atoms.
 * The parameters array must hold as many atoms as the actors tuple.
 * The generated parameter numbers are always equal to the tuple index (1-based), so
 * each actor is mapped to a distinct parameter, and any repeated variable loses its
 * equality constraint.
 */
void ActorsToParameters(TypedTuple const * actors, Atom parameters[]);

void PrintParameter(Atom parameter);


#endif	// PARAMETER_H
