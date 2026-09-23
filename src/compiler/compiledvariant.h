#ifndef COMPILED_VARIANT_H
#define COMPILED_VARIANT_H

#include "kernel/operator.h"
#include "kernel/typedtuple.h"
#include "kernel/Parameter.h"
#include "kernel/ServiceRegistry.h"

/**
 * A CompiledVariant is a compiled operator with its resolved query parameters (signature).
 * One variant is emitted per distinct parameter signature; clauses that
 * resolve to the same signature are combined with a UNION operator.
 */

typedef struct s_CompiledVariant {
	// resolved query parameters
	Atom parameters[RELATION_MAX_ARITY];
	Operator * op;
	// whether this variant was derived from a recursive clause (and contains a FIXPOINT operator)
	bool isRecursive;
	// whether this variant is an existing, primitive service
	bool isSeed;
	// whether this variant replaces an existing service
	bool isReplaced;
} CompiledVariant;

/**
 * Extract the type signature of a compiled variant, from the resolved parameters.
 */
TypeSignature CompiledVariantGetTypeSignature(CompiledVariant const * variant, size8 arity);

/**
 * Extract the IO signature of a compiled variant, from the resolved parameters.
 */
IOSignature CompiledVariantGetIOSignature(CompiledVariant const * variant, size8 arity);

/**
 * CLAUDE: Extract the equality signature of a compiled variant, from the resolved parameters.
 * The arity is the number of resolved parameters, which is the arity of the query; the
 * operator of the variant takes one argument per distinct parameter.
 */
EqualitySignature CompiledVariantGetEqualitySignature(CompiledVariant const * variant, size8 arity);

/**
 * Find a compiled variant in the given array whose signature matches the given parameters.
 */
CompiledVariant * FindCompiledVariant(
	CompiledVariant variants[], size8 nVariants, Atom parameters[], size8 nParameters);

/**
 * Setup a compiled variant from an existing service.
 */
void SetupCompiledVariantFromServiceRecord(CompiledVariant * variant, ServiceRecord const * serviceRecord);


#endif 	// COMPILED_VARIANT_H
