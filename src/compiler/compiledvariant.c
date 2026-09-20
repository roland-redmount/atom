
#include "compiler/compiledvariant.h"
#include "kernel/Relation.h"


TypeSignature CompiledVariantGetTypeSignature(CompiledVariant const * variant)
{
	return ParametersGetTypeSignature(variant->parameters, variant->op->nArguments);
}


IOSignature CompiledVariantGetIOSignature(CompiledVariant const * variant)
{
	return ParametersGetIOSignature(variant->parameters, variant->op->nArguments);
}


CompiledVariant * FindCompiledVariant(
	CompiledVariant variants[], size8 nVariants, Atom parameters[], size8 nParameters)
{
	for(index8 i = 0; i < nVariants; i++) {
		if(SameParameterSignature(variants[i].parameters, parameters, nParameters))
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


void CompiledVariantSeedFromService(CompiledVariant * variant, Service service)
{
	variant->relation = service.relation;
	variant->replacedOperator = variant->op = FindServiceOperator(service);

	for(index8 i = 0; i < variant->op->nArguments; i++) {
		variant->parameters[i] = (Atom) {
			.parameter = {
				.number = i + 1,
				.atomType = service.relation.typeSignature.atomTypes[i],
				.io = service.ioSignature.parameterIO[i]
			}
		};
	}
	ASSERT(SameTypeSignatures(
		CompiledVariantGetTypeSignature(variant), service.relation.typeSignature))
}


size8 DiscardUnusedSeedVariants(CompiledVariant variants[], size8 nVariants)
{
	// Remove variants, compacting the array starting from the end 
	for(index8 v = nVariants; v > 0; v--) {
		CompiledVariant * variant = &(variants[v - 1]);
		// A clause compiling into the variant always replaces the operator, since
		// unionOperators() allocates a new one
		// TODO: this test seems needlessly complex. A "seed" variant must be a primitive service,
		// so we should simply discard any variants with primitive services.
		if(!variant->replacedOperator || (variant->op != variant->replacedOperator))
			continue;
		ASSERT(!variant->isRecursive)
		for(index8 i = v; i < nVariants; i++)
			variants[i - 1] = variants[i];
		nVariants--;
	}
	return nVariants;
}
