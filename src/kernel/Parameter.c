
#include "kernel/Parameter.h"
#include "parser/Characters.h"


IOSignature CreateIOSignature(byte const parameterIO[], size8 nColumns)
{
	ASSERT(nColumns <= RELATION_MAX_ARITY)
	IOSignature ioSignature = {.parameterIO = {0}};
	CopyMemory(parameterIO, ioSignature.parameterIO, nColumns);
	return ioSignature;
}


/**
 * Test whether a tuple contains a parameter.
 */
static bool hasParameterAtom(TypedTuple const * tuple)
{
	for(index8 i = 0; i < tuple->nAtoms; i++) {
		if(TypedTupleGetElement(tuple, i).type == AT_PARAMETER)
			return true;
	}
	return false;
}


void ActorsToParameters(TypedTuple const * actors, Atom parameters[])
{
	ASSERT(!hasParameterAtom(actors))
	for(index8 i = 0; i < actors->nAtoms; i++) {
		TypedAtom typedAtom = TypedTupleGetElement(actors, i);
		if(typedAtom.type == AT_VARIABLE)
			parameters[i] = (Atom) {
				.parameter = {.number = i + 1, .io = PARAMETER_OUT, .atomType = 0}
			};
		else
			parameters[i] = (Atom) {
				.parameter = {.number = i + 1, .io = PARAMETER_IN, .atomType = typedAtom.type}
			};
	}
}


void PrintParameter(Atom atom)
{
	PrintF("@%u", atom.parameter.number);
	switch(atom.parameter.io) {
	case PARAMETER_IN:
		PrintChar('<');
		break;
	case PARAMETER_OUT:
		PrintChar('>');
		break;
	}
	PrintCString(GetAtomTypeName(atom.parameter.atomType));
}
