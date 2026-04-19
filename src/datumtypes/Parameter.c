
#include "datumtypes/Parameter.h"
#include "parser/Characters.h"

typedef union
{
	struct {
		byte io;
		byte atomType;
	} fields;
	data64 value;
} Parameter;


TypedAtom CreateParameter(byte io, byte type)
{
	Parameter param = {0};
	param.fields.io = io;
	param.fields.atomType = type;
	return (TypedAtom) {.type = AT_PARAMETER, .atom = param.value};
}

bool IsParameter(TypedAtom a)
{
	return a.type == AT_PARAMETER;
}


void PrintParameter(TypedAtom parameter)
{
	Parameter param;
	param.value = parameter.atom;
	if(param.fields.io == PARAMETER_IN)
		PrintChar('@');
	else
		PrintChar('$');
	if(param.fields.atomType) {
		PrintCString(GetAtomTypeName(param.fields.atomType));
	}
}


int8 CompareParameters(Atom parameter1, Atom parameter2)
{
	Parameter param1;
	param1.value = parameter1;
	Parameter param2;
	param2.value = parameter2;
	
	if(param1.fields.atomType && (param1.fields.atomType < param2.fields.atomType))
		return -1;
	else if(param2.fields.atomType && (param1.fields.atomType > param2.fields.atomType))
		return 1;
	else {
		if(param1.fields.io == PARAMETER_IN_OUT || param2.fields.io == PARAMETER_IN_OUT ||
		 param1.fields.io == param2.fields.io)
			return 0;
		else if(param1.fields.io < param2.fields.io)
			return -1;
		else
			return 1;
	}
}

