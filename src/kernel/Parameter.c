
#include "kernel/Parameter.h"
#include "lang/Variable.h"
#include "parser/Characters.h"


IOSignature CreateIOSignature(byte const parameterIO[], size8 nParameters)
{
	ASSERT(nParameters <= RELATION_MAX_ARITY)
	IOSignature ioSignature = {.parameterIO = {0}};
	CopyMemory(parameterIO, ioSignature.parameterIO, nParameters);
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


/**
 * Find the index of the first actor in the tuple that is the same variable as
 * the actor at the given index. Returns the given index if there is no earlier such actor.
 */
static index8 findFirstVariableOccurrence(TypedTuple const * actors, index8 index)
{
	TypedAtom variable = TypedTupleGetElement(actors, index);
	ASSERT(variable.type == AT_VARIABLE)
	for(index8 i = 0; i < index; i++) {
		TypedAtom actor = TypedTupleGetElement(actors, i);
		if((actor.type == AT_VARIABLE) && SameVariable(actor.atom, variable.atom))
			return i;
	}
	return index;
}


void ActorsToParameters(TypedTuple const * actors, Atom parameters[])
{
	ASSERT(!hasParameterAtom(actors))
	uint8 nextNumber = 1;
	for(index8 i = 0; i < actors->nAtoms; i++) {
		TypedAtom typedAtom = TypedTupleGetElement(actors, i);
		if(typedAtom.type == AT_VARIABLE) {
			index8 first = findFirstVariableOccurrence(actors, i);
			if(first < i)
				parameters[i] = parameters[first];
			else
				parameters[i] = (Atom) {
					.parameter = {.number = nextNumber++, .io = PARAMETER_OUT, .atomType = 0}
				};
		}
		else
			parameters[i] = (Atom) {
				.parameter = {.number = nextNumber++, .io = PARAMETER_IN, .atomType = typedAtom.type}
			};
	}
}


TypeSignature ParametersGetTypeSignature(Atom const parameters[], size8 nParameters)
{
	byte atomTypes[nParameters];
	for(index8 i = 0; i < nParameters; i++)
		atomTypes[i] = parameters[i].parameter.atomType;
	return CreateTypeSignature(atomTypes, nParameters);
}


IOSignature ParametersGetIOSignature(Atom const parameters[], size8 nParameters)
{
	byte parameterIO[nParameters];
	for(index8 i = 0; i < nParameters; i++)
		parameterIO[i] = parameters[i].parameter.io;
	return CreateIOSignature(parameterIO, nParameters);	
}


bool SameParameterSignature(Atom const first[], Atom const second[], size8 nParameters)
{
	for(index8 i = 0; i < nParameters; i++) {
		if(first[i].parameter.atomType != second[i].parameter.atomType)
			return false;
		if(first[i].parameter.io != second[i].parameter.io)
			return false;
	}
	return true;
}


bool SameParameterRepeats(Atom const first[], Atom const second[], size8 nParameters)
{
	for(index8 i = 0; i < nParameters; i++) {
		for(index8 j = 0; j < i; j++) {
			bool firstRepeats = (first[i].parameter.number == first[j].parameter.number);
			bool secondRepeats = (second[i].parameter.number == second[j].parameter.number);
			if(firstRepeats != secondRepeats)
				return false;
		}
	}
	return true;
}


EqualitySignature ParametersGetEqualitySignature(Atom const parameters[], size8 nParameters)
{
	ASSERT(nParameters <= RELATION_MAX_ARITY)
	EqualitySignature equalitySignature = {.repeatOf = {0}};
	for(index8 i = 0; i < nParameters; i++) {
		for(index8 j = 0; j < i; j++) {
			if(parameters[j].parameter.number == parameters[i].parameter.number) {
				equalitySignature.repeatOf[i] = j + 1;
				break;
			}
		}
	}
	return equalitySignature;
}


bool HasRepeatedParameters(EqualitySignature equalitySignature)
{
	for(index8 i = 0; i < RELATION_MAX_ARITY; i++) {
		if(equalitySignature.repeatOf[i])
			return true;
	}
	return false;
}


size8 EqualitySignatureGetArgumentMap(
	EqualitySignature equalitySignature, size8 nColumns, index8 * argumentMap)
{
	ASSERT(nColumns <= RELATION_MAX_ARITY)
	size8 nArguments = 0;
	for(index8 i = 0; i < nColumns; i++) {
		uint8 repeatOf = equalitySignature.repeatOf[i];
		if(repeatOf) {
			ASSERT(repeatOf <= i)
			argumentMap[i] = argumentMap[repeatOf - 1];
		}
		else
			argumentMap[i] = nArguments++;
	}
	return nArguments;
}


size8 ParametersGetArgumentMap(Atom const parameters[], size8 nParameters, index8 * argumentMap)
{
	return EqualitySignatureGetArgumentMap(
		ParametersGetEqualitySignature(parameters, nParameters), nParameters, argumentMap);
}


size8 RenumberParameters(Atom parameters[], size8 nParameters)
{
	index8 argumentMap[nParameters];
	size8 nArguments = ParametersGetArgumentMap(parameters, nParameters, argumentMap);
	for(index8 i = 0; i < nParameters; i++)
		parameters[i].parameter.number = argumentMap[i] + 1;
	return nArguments;
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
