
#include "datumtypes/Parameter.h"
#include "parser/Characters.h"

typedef union
{
	struct {
		char name;
		byte io;
		byte type;
	} fields;
	data64 value;
} Parameter;


Atom CreateParameter(char name, byte io, byte type)
{
	// for now we just store a single lowercase character _x, _y, ...
	ASSERT(IsAlpha(name));
	Parameter arg = {.value = 0};
	arg.fields.name = ToLower(name);
	arg.fields.io = io;
	arg.fields.type = type;
	return (Atom) {DT_PARAMETER, 0, 0, 0, arg.value};
}

bool IsParameter(Atom a)
{
	return a.type == DT_PARAMETER;
}


char GetParameterName(Atom parameter)
{
	Parameter arg;
	arg.value = parameter.datum;
	return arg.fields.name;
}


void PrintParameter(Atom parameter)
{
	Parameter arg;
	arg.value = parameter.datum;
	PrintChar('$');
	PrintChar(arg.fields.name);
	if(arg.fields.io == PARAMETER_IN)
		PrintChar('<');
	else
		PrintChar('>');
	if(arg.fields.type)
		PrintCString(GetDatumTypeName(arg.fields.type));
}
