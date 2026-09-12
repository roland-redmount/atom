
#include "compiler/compiledvariant.h"


TypeSignature CompiledVariantGetTypeSignature(CompiledVariant const * variant)
{
	size8 arity = variant->parameters->nAtoms;
	byte atomTypes[arity];
	Atom const * parameters = TypedTuplePeekAtoms(variant->parameters);
	for(index8 i = 0; i < arity; i++)
		atomTypes[i] = parameters[i].parameter.atomType;
	return CreateTypeSignature(atomTypes, arity);
}


IOSignature CompiledVariantGetIOSignature(CompiledVariant const * variant)
{
	size8 arity = variant->parameters->nAtoms;
	byte parameterIO[arity];
	Atom const * parametersArray = TypedTuplePeekAtoms(variant->parameters);
	for(index8 i = 0; i < arity; i++)
		parameterIO[i] = parametersArray[i].parameter.io;
	return CreateIOSignature(parameterIO, arity);
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
	// Check if relation is already set, so that we don't acquire double references
	if(!IsNullRelation(variant->relation))
		return;
	variant->relation = CreateRelation(queryTermForm, CompiledVariantGetTypeSignature(variant));
}


void CompiledVariantSeedFromService(
	CompiledVariant * variant, Service const * service, TypedTuple const * queryParameters)
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
						.atomType = service->relation.typeSignature.atomTypes[i],
						.io = parameters[i].parameter.io
					}
				}
			)
		);
	}
	ASSERT(SameTypeSignatures(
		CompiledVariantGetTypeSignature(variant), service->relation.typeSignature))

	variant->relation = service->relation;
	AcquireRelation(variant->relation);
	variant->op = service->op;
	variant->replacedOperator = service->op;
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
		ReleaseRelation(variant->relation);
		for(index8 i = v; i < nVariants; i++)
			variants[i - 1] = variants[i];
		nVariants--;
	}
	return nVariants;
}
