
#include "lang/Variable.h"
#include "parser/Characters.h"

typedef union
{
	struct {
		char name;
		byte type;
		uint8 quoteCount;
	} fields;
	data64 value;
} Variable;


TypedAtom anonymousVariable = {.type = AT_VARIABLE, .atom = 0};


TypedAtom CreateVariable(char name)
{
	// for now we just store a single lowercase character _x, _y, ...
	ASSERT(IsAlpha(name));
	Variable var = {0};
	var.fields.name = ToLower(name);
	var.fields.quoteCount = 0;
	return (TypedAtom) {.type = AT_VARIABLE, .atom = var.value};
}


TypedAtom CreateTypedVariable(char name, byte type)
{
	// for now we just store a single lowercase character _x, _y, ...
	ASSERT(IsAlpha(name));
	Variable var = {0};
	var.fields.name = ToLower(name);
	var.fields.type = type;
	var.fields.quoteCount = 0;
	return (TypedAtom) {.type = AT_VARIABLE, .atom = var.value};
}


char GetVariableName(TypedAtom variable)
{
	Variable var;
	var.value = variable.atom;
	if(var.fields.name)
		return var.fields.name;
	else
		return '_';
}


byte VariableGetType(Atom variable)
{
	Variable var;
	var.value = variable;
	return var.fields.type;
}


bool IsVariable(TypedAtom a)
{
	return a.type == AT_VARIABLE;
}


bool SameVariable(TypedAtom variable1, TypedAtom variable2)
{
	if(variable1.atom && variable2.atom)
		return variable1.atom == variable2.atom;
	else {
		// either variable is _
		return 0;
	}
}


bool VariableIsQuoted(TypedAtom variable)
{
	Variable var;
	var.value = variable.atom;
	return var.fields.quoteCount > 0;	
}

TypedAtom QuoteVariable(TypedAtom variable)
{
	Variable var;
	var.value = variable.atom;
	var.fields.quoteCount++;
	// check for uint8 wraparound
	ASSERT(var.fields.quoteCount > 0);

	return (TypedAtom) {.type = AT_VARIABLE, .atom = var.value};
}


TypedAtom UnquoteVariable(TypedAtom variable)
{
	
	Variable var;
	var.value = variable.atom;
	ASSERT(var.fields.quoteCount > 0);
	var.fields.quoteCount--;
	return (TypedAtom) {.type = AT_VARIABLE, .atom = var.value};
}


bool VariableMatch(Atom variable, TypedAtom typedAtom)
{
	if(!variable)
		return true;	// anonymous variable
	Variable var;
	var.value = variable;
	if(var.fields.type)
		return var.fields.type == typedAtom.type;
	else
		return true;
}


void PrintVariable(TypedAtom variable)
{
	Variable var;
	var.value = variable.atom;
	if(var.value) {
		for(uint8 i = 0; i < var.fields.quoteCount; i++)
			PrintChar('\'');
		PrintChar(var.fields.name);
		if(var.fields.type)
			PrintF(":%s", GetAtomTypeName(var.fields.type));
	}
	else {
		// anonymous variable
		PrintChar('_');
	}

}

