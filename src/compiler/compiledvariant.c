
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

