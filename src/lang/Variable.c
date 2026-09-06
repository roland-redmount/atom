
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
	if(variable1.variable.name || variable2.variable.name)
		return SameAtoms(variable1, variable2);
	else {
		// both variables are _, which compares unequal to itself
		return false;
	}
}


bool VariableIsQuoted(Atom variable)
{
	return variable.variable.quoted;
}


Atom QuoteVariable(Atom variable)
{
	// A variable cannot be quoted twice
	ASSERT(!variable.variable.quoted);
	return (Atom) {
		.variable = {
			.name = variable.variable.name,
			.quoted = true
		}
	};
}


Atom UnquoteVariable(Atom variable)
{
	ASSERT(variable.variable.quoted);
	return (Atom) {
		.variable = {
			.name = variable.variable.name,
			.quoted = false
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
	if(variable.variable.quoted)
		PrintChar('^');
	if(variable.variable.name)
		PrintChar(variable.variable.name);
	else
		PrintChar('_');
}

