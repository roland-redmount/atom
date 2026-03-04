/**
 * Variables are used in queries to indicate "any" atom.
 */

#ifndef	VARIABLE_H
#define	VARIABLE_H 

#include "lang/Atom.h"
#include "lang/Datum.h"

/**
 * Create a variable with given name.
 * 
 * NOTE: variables could internally be referred to 
 * by their index (order) in the formula in which they reside.
 * The character (or name) is for user readability only.
 */
Atom CreateVariable(char name);

/**
 * The anonymous variable _
 * This is a bit of a hack.
 * Each occurence _ is interpreted as a distinct variable.
 * The anonymous variable cannot be quoted; since it compares
 * unequal to all other variables, it cannot be queried for
 * with e.g. (foo '_)
 */
extern Atom anonymousVariable;

bool IsVariable(Atom a);

/**
 * Compare variables, such that the anonymous variable _
 * compares unequal to any other variable, and to itself.
 */
bool SameVariable(Atom variable1, Atom variable2);

/**
 * Get the variable name, or '_' for the anonymous variable.
 */
char GetVariableName(Atom variable);


/**
 * Handle quoted variables
 */
bool VariableIsQuoted(Atom variable);
Atom QuoteVariable(Atom variable);
Atom UnquoteVariable(Atom variable);

void PrintVariable(Atom variable);

 // Atom ParseVariable(char const * syntax, size32 length);

#endif	// VARIABLE_H
