
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


void SetupCompiledVariantFromServiceRecord(CompiledVariant * variant, ServiceRecord const * serviceRecord)
{
	variant->op = serviceRecord->op;
	ASSERT(variant->op->type == OPERATOR_MACHINE)
	variant->isSeed = true;

	TypeSignature typeSignature = serviceRecord->service.relation.typeSignature;
	for(index8 i = 0; i < variant->op->nArguments; i++) {
		variant->parameters[i] = (Atom) {
			.parameter = {
				.number = i + 1,
				.atomType = typeSignature.atomTypes[i],
				.io = serviceRecord->service.ioSignature.parameterIO[i]
			}
		};
	}
	ASSERT(SameTypeSignatures(CompiledVariantGetTypeSignature(variant), typeSignature))
}
