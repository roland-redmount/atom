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
	// resolved query parameters, owned by the variant
	TypedTuple * parameters;
	Operator * op;
	// The relation this variant compiles to, and a reference to it. Created before the
	// recursive clauses compile, as their recursive term reads it.
	Relation relation;
	// whether this variant was derived from a recursive clause (and contains a FIXPOINT operator)
	bool isRecursive;

	// CLAUDE: The operator of the service this variant was seeded from, if any; else 0.
	// A seeded variant replaces that service in the registry, holding its operator as a
	// branch of the union it compiles into; see CompiledVariantSeedFromService().
	Operator * replacedOperator;
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
	CompiledVariant variants[], size8 nVariants, TypedTuple const * parameters);

/**
 * Set the relation for a CompiledVariant, determined from the given query term form
 * and the variant's type signature.
 */
void CompiledVariantSetRelation(CompiledVariant * variant, Atom queryTermForm);

/**
 * CLAUDE: Seed a compiled variant from an existing service answering the query, so that
 * the clauses compiling for the query union into that service rather than register a
 * second service of its signature. The variant takes the signature of the service: the
 * types of the service's relation, and the IO direction of the query parameters.
 *
 * The variant is registered against the service's own relation, whose columns are in
 * relation order, which is what lets the compiled service take over the service's
 * registry key; see seedVariantsFromServices() in compiler.c. The queryParameters tuple
 * must therefore be in that same order.
 *
 * The variant borrows the service's operator without taking a reference. The union the
 * clauses compile into is what takes one; a variant nothing compiled into owes nothing
 * and is dropped by DiscardUnusedSeedVariants().
 */
void CompiledVariantSeedFromService(
	CompiledVariant * variant, Service const * service, TypedTuple const * queryParameters);

/**
 * CLAUDE: Drop the seeded variants that no clause compiled into, and return the new
 * number of variants. Such a variant is nothing but the service it was seeded from,
 * which answers the query as it stands.
 */
size8 DiscardUnusedSeedVariants(CompiledVariant variants[], size8 nVariants);

#endif 	// COMPILED_VARIANT_H
