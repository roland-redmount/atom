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
 * An IO signature (of a service) is the IO direction of each of its parameters
 */
typedef struct s_IOSignature {
	byte parameterIO[RELATION_MAX_ARITY];
} IOSignature;

/**
 * Create an IO signature from given parameter IO array.
 * The signature is zero filled beyond nParameters.
 */
IOSignature CreateIOSignature(byte const parameterIO[], size8 nParameters);

/**
 * An equality signature of a service records which parameters are identical,
 * expressing an equality constaint. repeatOf[i] is the position (1-based) of the first
 * parameter that is identical to parameter i, or 0 if the parameter occurs for the first time.
 * For example, the service (foo @1 bar @2 baz @1) has repeatOf = {0, 0, 1}, since the
 * third parameter @1 is identical to the first. A zero signature therefore means no repeated
 * parameters, which is the case for every primitive service.
 */
typedef struct s_EqualitySignature {
	uint8 repeatOf[RELATION_MAX_ARITY];
} EqualitySignature;

/**
 * Extract an EqualitySignature from an array of AT_PARAMETER atoms.
 */
EqualitySignature ParametersGetEqualitySignature(Atom const parameters[], size8 nParameters);

/**
 * Test whether an equality signature has any repeated parameter.
 */
bool HasRepeatedParameters(EqualitySignature equalitySignature);

/**
 * Compute an argument map from an equality signature over nParameters, such that argumentMap[i]
 * is the index of the argument corresponding to parameter i, where the arguments are numbered in
 * order of first occurrence. The service (foo @1 bar @2 baz @1) has argumentMap = {0, 1, 0}.
 * The argument map format is used by the CONSTRAIN operator; see CreateConstrainOperator().
 * Returns the number of arguments, which is the same as the number of unique parameters.
 */
size8 EqualitySignatureGetArgumentMap(
	EqualitySignature equalitySignature, size8 nParameters, index8 argumentMap[]);

/**
 * Same as EqualitySignatureGetArgumentMap(), for an array of AT_PARAMETER atoms.
 * Parameters with the same number take the same argument.
 */
size8 ParametersGetArgumentMap(Atom const parameters[], size8 nParameters, index8 argumentMap[]);

/**
 * CLAUDE: Renumber an array of AT_PARAMETER atoms 1, 2, ... in order of first occurrence,
 * so that parameters with the same number keep sharing a number.
 * Returns the number of distinct parameters.
 */
size8 RenumberParameters(Atom parameters[], size8 nParameters);

/**
 * Extract a TypeSignature from an array of AT_PARAMETER atoms.
 */
TypeSignature ParametersGetTypeSignature(Atom const parameters[], size8 nParameters);

/**
 * Extract an IOSignature from an array of AT_PARAMETER atoms.
 */
IOSignature ParametersGetIOSignature(Atom const parameters[], size8 nParameters);

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
/* CLAUDE: The parameter numbers are now 1, 2, ... in order of first occurrence, and every
 * occurrence of a repeated variable takes the parameter number of its first occurrence.
 * The service answering the query therefore keeps the equality constraint; see
 * EqualitySignature. */
void ActorsToParameters(TypedTuple const * actors, Atom parameters[]);

/**
 * Two parameter tuples denote the same service signature if they agree on
 * the type and direction of every parameter; parameter numbers are ignored here.
 */
bool SameParameterSignature(Atom const first[], Atom const second[], size8 nParameters);

/**
 * CLAUDE: Test whether two parameter tuples repeat parameters at the same positions,
 * so that parameters i and j have the same number in the first tuple exactly when they
 * have the same number in the second tuple.
 */
bool SameParameterRepeats(Atom const first[], Atom const second[], size8 nParameters);

/**
 * Find the indices of the input (PARAMETER_IN) parameters in the IO signature
 * and write into the inputArguments array, which must hold RELATION_MAX_ARITY indices.
 * Returns the number of inputs found.
 */
size8 FindInputArguments(IOSignature ioSignature, size8 arity, index8 inputArguments[]);

void PrintParameter(Atom parameter);


#endif	// PARAMETER_H
