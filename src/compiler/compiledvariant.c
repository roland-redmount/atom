
#include "compiler/compiledvariant.h"
#include "kernel/Relation.h"


TypeSignature CompiledVariantGetTypeSignature(CompiledVariant const * variant)
{
	return ParametersGetTypeSignature(
		TypedTuplePeekAtoms(variant->parameters), variant->parameters->nAtoms);
}


IOSignature CompiledVariantGetIOSignature(CompiledVariant const * variant)
{
	return ParametersGetIOSignature(
		TypedTuplePeekAtoms(variant->parameters), variant->parameters->nAtoms);
}


CompiledVariant * FindCompiledVariant(
	CompiledVariant variants[], size8 nVariants, TypedTuple const * parameters)
{
	for(index8 i = 0; i < nVariants; i++) {
		if(SameParameterSignature(variants[i].parameters, parameters))
			return &(variants[i]);
	}
	return 0;
}


void CompiledVariantSetRelation(CompiledVariant * variant, Atom queryTermForm)
{
	// if relation is already set, we do nothing
	if(!IsNullRelation(variant->relation))
		return;
	variant->relation = (Relation) {
		.termForm = queryTermForm, .typeSignature = CompiledVariantGetTypeSignature(variant)};
}


void CompiledVariantSeedFromService(
	CompiledVariant * variant, Service service, TypedTuple const * queryParameters)
{
	size8 arity = queryParameters->nAtoms;
	Atom const * parameters = TypedTuplePeekAtoms(queryParameters);
	variant->parameters = CreateTypedTuple(arity);
	for(index8 i = 0; i < arity; i++) {
		TypedTupleSetElement(variant->parameters, i,
			CreateTypedAtom(
				AT_PARAMETER,
				(Atom) {
					.parameter = {
						.number = i + 1,
						.atomType = service.relation.typeSignature.atomTypes[i],
						.io = parameters[i].parameter.io
					}
				}
			)
		);
	}
	ASSERT(SameTypeSignatures(
		CompiledVariantGetTypeSignature(variant), service.relation.typeSignature))

	variant->relation = service.relation;
	variant->replacedOperator = variant->op = FindServiceOperator(service);
}


size8 DiscardUnusedSeedVariants(CompiledVariant variants[], size8 nVariants)
{
	// Walk downwards, so that compacting the array cannot move a variant past the
	// position being examined
	for(index8 v = nVariants; v > 0; v--) {
		CompiledVariant * variant = &(variants[v - 1]);
		// A clause compiling into the variant always replaces the operator, since
		// unionOperators() allocates a new one
		if(!variant->replacedOperator || (variant->op != variant->replacedOperator))
			continue;
		ASSERT(!variant->isRecursive)
		// The variant owns its parameters and a reference to the relation. Its operator
		// still belongs to the service it was seeded from, so there is nothing to release.
		FreeTypedTuple(variant->parameters);
		// ReleaseRelation(variant->relation);
		for(index8 i = v; i < nVariants; i++)
			variants[i - 1] = variants[i];
		nVariants--;
	}
	return nVariants;
}
