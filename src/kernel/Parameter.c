
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


bool SameParameterSignature(TypedTuple const * first, TypedTuple const * second)
{
	ASSERT(first->nAtoms == second->nAtoms)
	for(index8 i = 0; i < first->nAtoms; i++) {
		TypedAtom a = TypedTupleGetElement(first, i);
		TypedAtom b = TypedTupleGetElement(second, i);
		ASSERT((a.type == AT_PARAMETER) && (b.type == AT_PARAMETER))
		if(a.atom.parameter.atomType != b.atom.parameter.atomType)
			return false;
		if(a.atom.parameter.io != b.atom.parameter.io)
			return false;
	}
	return true;
}


size8 FindInputArguments(IOSignature ioSignature, size8 arity, index8 inputArguments[])
{
	ASSERT(arity <= RELATION_MAX_ARITY)
	size8 nInputs = 0;
	for(index8 i = 0; i < arity; i++) {
		if(ioSignature.parameterIO[i] == PARAMETER_IN)
			inputArguments[nInputs++] = i;
	}
	return nInputs;
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
