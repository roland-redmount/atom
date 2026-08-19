/**
 * An AT_PARAMETER atom is used in service signatures to mark a position
 * in the actor list where a parameter is a expected. Parameters are
 * different from variables: while a variable indicates "any atom"
 * and has no particular input/output direction, an parameter
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
 * Generate a parameters tuple from an actors tuple, such that each non-variable atom
 * in the actors tuple yields an input parameter of the same type as the atom,
 * and each variable yields an output parameter, whose type is unknown.
 * The two tuples must have the same number of atoms.
 * The output parameter types must be discovered later by matching against services.
 * The generated parameter numbers are always equal to the tuple index (1-based), so
 * actor is mapped to a distinct parameter, and any repeated variable loses its
 * equality constraint.
 *
 * The actors are those of a query, and so hold no parameter of their own, which DEBUG
 * builds assert. A caller working in parameters already, as the compiler does, has
 * nothing to generalize.
 */
void GetQueryParameters(TypedTuple const * actors, TypedTuple * parameters);

void PrintParameter(Atom parameter);


#endif	// PARAMETER_H
