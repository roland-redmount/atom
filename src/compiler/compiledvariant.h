#ifndef COMPILED_VARIANT_H
#define COMPILED_VARIANT_H

#include "kernel/operator.h"
#include "kernel/typedtuple.h"
#include "kernel/ServiceRegistry.h"		// for IOSignature

/**
 * A CompiledVariant is a compiled operator with its resolved query parameters (signature).
 * One variant is emitted per distinct parameter signature; clauses that
 * resolve to the same signature are combined with a UNION operator.
 */

typedef struct s_CompiledVariant {
	// resolved query parameters, owned by the variant
	TypedTuple * parameters;
	Operator * op;
	// The relation this variant compiles to, and a reference to it. Created before the
	// recursive clauses compile, as their recursive term reads it.
	Relation relation;
	// whether this variant was derived from a recursive clause (and contains a FIXPOINT operator)
	bool isRecursive;
} CompiledVariant;


/**
 * Extract the type signature of a compiled variant, from the resolved parameters.
 */
TypeSignature CompiledVariantGetTypeSignature(CompiledVariant const * variant);

/**
 * Extractthe IO signature of a compiled variant, from the resolved parameters.
 */
IOSignature CompiledVariantGetIOSignature(CompiledVariant const * variant);

/**
 * Find a compiled variant in the given array whose signature matches the given parameters.
 */
CompiledVariant * FindCompiledVariant(
	CompiledVariant variants[], size8 nVariants, TypedTuple const * parameters);

/**
 * Set the relation for a CompiledVariant, determined from the given query term form
 * and the variant's type signature.
 */
void CompiledVariantSetRelation(CompiledVariant * variant, Atom queryTermForm);


#endif 	// COMPILED_VARIANT_H
