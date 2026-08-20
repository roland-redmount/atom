
#include "lang/Variable.h"
#include "parser/Characters.h"


TypedAtom anonymousVariable = {.type = AT_VARIABLE, .atom = {0}};


Atom CreateVariable(char name)
{
	ASSERT(IsAlpha(name));
	return (Atom) {
		.variable = {.name = ToLower(name)}
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
		return SameAtoms(variable1, variable2);
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
			.quoteCount = variable.variable.quoteCount - 1
		}
	};
}


// bool VariableMatch(Atom variable, TypedAtom typedAtom)
// {
// 	if(!variable.variable.name)
// 		return true;	// anonymous variable
// 	if(variable.variable.type)
// 		return variable.variable.type == typedAtom.type;
// 	else
// 		return true;
// }


void PrintVariable(Atom variable)
{
	for(uint8 i = 0; i < variable.variable.quoteCount; i++)
		PrintChar('\'');
	if(variable.variable.name)
		PrintChar(variable.variable.name);
	else
		PrintChar('_');
}

