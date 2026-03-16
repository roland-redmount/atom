
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


Atom CreateParameter(byte io, byte type)
{
	Parameter arg;
	arg.fields.io = io;
	arg.fields.datumType = type;
	return (Atom) {.type = DT_PARAMETER, .datum = arg.value};
}

bool IsParameter(Atom a)
{
	return a.type == DT_PARAMETER;
}


void PrintParameter(Atom parameter)
{
	Parameter arg;
	arg.value = parameter.datum;
	if(arg.fields.io == PARAMETER_IN)
		PrintChar('@');
	else
		PrintChar('$');
	if(arg.fields.datumType) {
		PrintCString(GetDatumTypeName(arg.fields.datumType));
	}
}
