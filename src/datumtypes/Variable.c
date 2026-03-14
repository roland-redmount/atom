
#include "datumtypes/Variable.h"
#include "parser/Characters.h"

typedef union
{
	struct {
		char name;
		uint8 quoteCount;
	} fields;
	data64 value;
} Variable;


Atom anonymousVariable = {.type = DT_VARIABLE, .datum = 0};


Atom CreateVariable(char name)
{
	// for now we just store a single lowercase character _x, _y, ...
	ASSERT(IsAlpha(name));
	Variable var = {.value = 0};
	var.fields.name = ToLower(name);
	var.fields.quoteCount = 0;
	return (Atom) {.type = DT_VARIABLE, .datum = var.value};
}


char GetVariableName(Atom variable)
{
	Variable var;
	var.value = variable.datum;
	if(var.fields.name)
		return var.fields.name;
	else
		return '_';
}


bool IsVariable(Atom a)
{
	return a.type == DT_VARIABLE;
}


bool SameVariable(Atom variable1, Atom variable2)
{
	if(variable1.datum & variable2.datum)
		return variable1.datum == variable2.datum;
	else {
		// either variable is _
		return 0;
	}
}


bool VariableIsQuoted(Atom variable)
{
	Variable var;
	var.value = variable.datum;
	return var.fields.quoteCount > 0;	
}

Atom QuoteVariable(Atom variable)
{
	Variable var;
	var.value = variable.datum;
	var.fields.quoteCount++;
	// check for uint8 wraparound
	ASSERT(var.fields.quoteCount > 0);

	return (Atom) {.type = DT_VARIABLE, .datum = var.value};
}


Atom UnquoteVariable(Atom variable)
{
	
	Variable var;
	var.value = variable.datum;
	ASSERT(var.fields.quoteCount > 0);
	var.fields.quoteCount--;
	return (Atom) {.type = DT_VARIABLE, .datum = var.value};
}


void PrintVariable(Atom variable)
{
	Variable var;
	var.value = variable.datum;
	for(uint8 i = 0; i < var.fields.quoteCount; i++)
		PrintChar('\'');
	PrintChar(var.fields.name);
}

