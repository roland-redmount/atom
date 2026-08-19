
#include "kernel/Parameter.h"
#include "parser/Characters.h"


/**
 * Test whether an actors tuple holds a parameter, which a query never does: a parameter
 * belongs to a signature, and is what a query is generalized to.
 */
static bool hasParameterAtom(TypedTuple const * actors)
{
	for(index8 i = 0; i < actors->nAtoms; i++) {
		if(TypedTupleGetElement(actors, i).type == AT_PARAMETER)
			return true;
	}
	return false;
}


void GetQueryParameters(TypedTuple const * actors, TypedTuple * parameters)
{
	ASSERT(actors->nAtoms == parameters->nAtoms)
	ASSERT(!hasParameterAtom(actors))
	for(index8 i = 0; i < actors->nAtoms; i++) {
		TypedAtom typedAtom = TypedTupleGetElement(actors, i);
		if(typedAtom.type == AT_VARIABLE) {
			Atom parameter = {
				.parameter = {.number = i + 1, .io = PARAMETER_OUT, .atomType = 0}
			};
			TypedTupleSetElement(parameters, i, CreateTypedAtom(AT_PARAMETER, parameter));
		}
		else {
			Atom parameter = {
				.parameter = {.number = i + 1, .io = PARAMETER_IN, .atomType = typedAtom.type}
			};
			TypedTupleSetElement(parameters, i, CreateTypedAtom(AT_PARAMETER, parameter));
		}
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
