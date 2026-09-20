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
TypeSignature CompiledVariantGetTypeSignature(CompiledVariant const * variant);

/**
 * Extract the IO signature of a compiled variant, from the resolved parameters.
 */
IOSignature CompiledVariantGetIOSignature(CompiledVariant const * variant);

/**
 * Find a compiled variant in the given array whose signature matches the given parameters.
 */
CompiledVariant * FindCompiledVariant(
	CompiledVariant variants[], size8 nVariants, Atom parameters[], size8 nParameters);

/**
 * Setup a compiled variant from an existing service.
 */
void CompiledVariantSeedFromService(CompiledVariant * variant, Service service);


#endif 	// COMPILED_VARIANT_H
