
#include "lang/Variable.h"
#include "parser/Characters.h"


TypedAtom anonymousVariable = {.type = AT_VARIABLE, .atom = {0}};

// Number of variables a--z; temporary variables are numbered after these
#define N_NAMED_VARIABLES	26


Atom CreateVariable(char name)
{
	ASSERT(IsAlpha(name));
	return (Atom) {
		.variable = {.number = ToLower(name) - 'a' + 1}
	};
}


Atom CreateTempVariable(uint8 number)
{
	ASSERT((number >= 1) && (number <= 255 - N_NAMED_VARIABLES))
	return (Atom) {
		.variable = {.number = N_NAMED_VARIABLES + number}
	};
}


char GetVariableName(Atom variable)
{
	ASSERT(variable.variable.number <= N_NAMED_VARIABLES)
	if(variable.variable.number)
		return 'a' + variable.variable.number - 1;
	else
		return '_';
}


bool SameVariable(Atom variable1, Atom variable2)
{
	if(variable1.variable.number || variable2.variable.number)
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
			.number = variable.variable.number,
			.quoted = true
		}
	};
}


Atom UnquoteVariable(Atom variable)
{
	ASSERT(variable.variable.quoted);
	return (Atom) {
		.variable = {
			.number = variable.variable.number,
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
	// A temporary variable is printed with its number, as _27
	if(variable.variable.number > N_NAMED_VARIABLES)
		PrintF("_%u", (unsigned) variable.variable.number);
	else
		PrintChar(GetVariableName(variable));
}

