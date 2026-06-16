
#include "lang/Variable.h"
#include "parser/Characters.h"


TypedAtom anonymousVariable = {.type = AT_VARIABLE, .atom = {0}};


Atom CreateVariable(char name)
{
	// for now we just store a single lowercase character _x, _y, ...
	ASSERT(IsAlpha(name));
	return (Atom) {
		.variable = {.name = name}
	};
}


Atom CreateTypedVariable(char name, byte type)
{
	// for now we just store a single lowercase character _x, _y, ...
	ASSERT(IsAlpha(name));
	return (Atom) {
		.variable = {.name = name, .type = type}
	};
}


char GetVariableName(Atom variable)
{
	if(variable.variable.name)
		return variable.variable.name;
	else
		return '_';
}


bool SameVariable(Atom variable1, Atom variable2)
{
	if(variable1.hash && variable1.hash)
		return variable1.hash == variable2.hash;
	else {
		// either variable is _
		return 0;
	}
}


bool VariableIsQuoted(Atom variable)
{
	return variable.variable.quoteCount > 0;	
}


Atom QuoteVariable(Atom variable)
{
	// guard against uint8 wraparound
	ASSERT(variable.variable.quoteCount < 255);
	return (Atom) {
		.variable = {
			.name = variable.variable.name,
			.type = variable.variable.type,
			.quoteCount = variable.variable.quoteCount + 1
		}
	};
}


Atom UnquoteVariable(Atom variable)
{
	ASSERT(variable.variable.quoteCount > 0);
	return (Atom) {
		.variable = {
			.name = variable.variable.name,
			.type = variable.variable.type,
			.quoteCount = variable.variable.quoteCount - 1
		}
	};
}


bool VariableMatch(Atom variable, TypedAtom typedAtom)
{
	if(!variable.variable.name)
		return true;	// anonymous variable
	if(variable.variable.type)
		return variable.variable.type == typedAtom.type;
	else
		return true;
}


void PrintVariable(Atom variable)
{
	for(uint8 i = 0; i < variable.variable.quoteCount; i++)
		PrintChar('\'');
	if(variable.variable.name)
		PrintChar(variable.variable.name);
	else
		PrintChar('_');
	if(variable.variable.type)
		PrintF(":%s", GetAtomTypeName(variable.variable.type));
}

