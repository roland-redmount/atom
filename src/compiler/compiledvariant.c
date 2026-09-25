
#include "compiler/compiledvariant.h"
#include "kernel/Relation.h"
#include "lang/formula.h"


TypeSignature CompiledVariantGetTypeSignature(CompiledVariant const * variant, size8 arity)
{
	return ParametersGetTypeSignature(variant->parameters, arity);
}


IOSignature CompiledVariantGetIOSignature(CompiledVariant const * variant, size8 arity)
{
	return ParametersGetIOSignature(variant->parameters, arity);
}


EqualitySignature CompiledVariantGetEqualitySignature(CompiledVariant const * variant, size8 arity)
{
	return ParametersGetEqualitySignature(variant->parameters, arity);
}


CompiledVariant * FindCompiledVariant(
	CompiledVariant variants[], size8 nVariants, Atom parameters[], size8 nParameters)
{
	for(index8 i = 0; i < nVariants; i++) {
		if(SameParameterSignature(variants[i].parameters, parameters, nParameters)
			&& SameParameterRepeats(variants[i].parameters, parameters, nParameters))
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
	size8 arity = FormArity(serviceRecord->service.relation.form);
	index8 argumentMap[arity];
	EqualitySignatureGetArgumentMap(serviceRecord->service.equalitySignature, arity, argumentMap);
	for(index8 i = 0; i < arity; i++) {
		variant->parameters[i] = (Atom) {
			.parameter = {
				.number = argumentMap[i] + 1,
				.atomType = typeSignature.atomTypes[i],
				.io = serviceRecord->service.ioSignature.parameterIO[i]
			}
		};
	}
	ASSERT(SameTypeSignatures(CompiledVariantGetTypeSignature(variant, arity), typeSignature))
}
