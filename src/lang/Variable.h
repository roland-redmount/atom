/**
 * Variables are used in queries to indicate "any" atom.
 */

#ifndef	VARIABLE_H
#define	VARIABLE_H 

#include "lang/TypedAtom.h"
#include "lang/Atom.h"

/**
 * Create a variable with given name.
 * 
 * NOTE: variables could internally be referred to 
 * by their index (order) in the formula in which they reside.
 * The character (or name) is for user readability only.
 * 
 * TODO: these functions should return Atom, not TypedAtom
 */
TypedAtom CreateVariable(char name);

/**
 * The anonymous variable _
 * This is a bit of a hack.
 * Each occurence _ is interpreted as a distinct variable.
 * The anonymous variable cannot be quoted; since it compares
 * unequal to all other variables, it cannot be queried for
 * with e.g. (foo '_)
 */
extern TypedAtom anonymousVariable;

bool IsVariable(TypedAtom a);

/**
 * Compare variables, such that the anonymous variable _
 * compares unequal to any other variable, and to itself.
 */
bool SameVariable(TypedAtom variable1, TypedAtom variable2);

/**
 * Get the variable name, or '_' for the anonymous variable.
 */
char GetVariableName(TypedAtom variable);


/**
 * Handle quoted variables
 */
bool VariableIsQuoted(TypedAtom variable);
TypedAtom QuoteVariable(TypedAtom variable);
TypedAtom UnquoteVariable(TypedAtom variable);

void PrintVariable(TypedAtom variable);

#endif	// VARIABLE_H
