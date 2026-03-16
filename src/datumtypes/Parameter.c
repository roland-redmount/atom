
#include "datumtypes/Parameter.h"
#include "parser/Characters.h"

typedef union
{
	struct {
		index8 index;
		byte io;
		byte type;
	} fields;
	data64 value;
} Parameter;


Atom CreateParameter(index8 index, byte io, byte type)
{
	ASSERT(index > 0);
	Parameter arg = {.value = 0};
	arg.fields.index = index;
	arg.fields.io = io;
	arg.fields.type = type;
	return (Atom) {DT_PARAMETER, 0, 0, 0, arg.value};
}

bool IsParameter(Atom a)
{
	return a.type == DT_PARAMETER;
}


index8 GetParameterIndex(Atom parameter)
{
	Parameter arg;
	arg.value = parameter.datum;
	return arg.fields.index;
}


void PrintParameter(Atom parameter)
{
	Parameter arg;
	arg.value = parameter.datum;
	if(arg.fields.io == PARAMETER_IN)
		PrintChar('@');
	else
		PrintChar('$');
	PrintChar(arg.fields.index);
	if(arg.fields.type) {
		PrintChar(':');
		PrintCString(GetDatumTypeName(arg.fields.type));
	}
}
