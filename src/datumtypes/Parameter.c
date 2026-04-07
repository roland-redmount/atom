
#include "datumtypes/Parameter.h"
#include "parser/Characters.h"

typedef union
{
	struct {
		byte io;
		byte datumType;
	} fields;
	data64 value;
} Parameter;


TypedAtom CreateParameter(byte io, byte type)
{
	Parameter arg;
	arg.fields.io = io;
	arg.fields.datumType = type;
	return (TypedAtom) {.type = DT_PARAMETER, .atom = arg.value};
}

bool IsParameter(TypedAtom a)
{
	return a.type == DT_PARAMETER;
}


void PrintParameter(TypedAtom parameter)
{
	Parameter arg;
	arg.value = parameter.atom;
	if(arg.fields.io == PARAMETER_IN)
		PrintChar('@');
	else
		PrintChar('$');
	if(arg.fields.datumType) {
		PrintCString(GetAtomTypeName(arg.fields.datumType));
	}
}
