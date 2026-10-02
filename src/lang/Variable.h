/**
 * Variables are used in formulas to indicate "any" atom.
 * A variables is identified by a single letter, case-insensitive,
 * or by a number for temporary variables (used by the compiler only).
 * If a variable occurs only once in a formula so that its
 * identity is irrelevant, an "anonymous" variable _ can be used.
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
 */
Atom CreateVariable(char name);

/**
 * Create a temporary variable, distinct from every variable a--z and from the
 * anonymous variable. The compiler uses temporary variables to rename  variables.
 * Temporary variables with the same number are the same variable.
 * The number must be at least 1, and at most 229.
 */
Atom CreateTempVariable(uint8 number);

/**
 * The anonymous variable _
 * This is a bit of a hack.
 * Each occurence _ is interpreted as a distinct variable.
 * The anonymous variable cannot be quoted; since it compares
 * unequal to all other variables, it cannot be queried for
 * with e.g. (foo '_)
 */
extern TypedAtom anonymousVariable;

/**
 * Compare variables, such that the anonymous variable _
 * compares unequal to any other variable, and to itself.
 */
bool SameVariable(Atom variable1, Atom variable2);

/**
 * Get the variable name, or '_' for the anonymous variable.
 * The variable must not be a temporary variable
 */
char GetVariableName(Atom variable);

/**
 * Determine if a variable matches an atom,
 * considering type if the variable is typed.
 */
// bool VariableMatch(Atom variable, TypedAtom typedAtom);

/**
 * Handle quoted variables
 */
bool VariableIsQuoted(Atom variable);
Atom QuoteVariable(Atom variable);
Atom UnquoteVariable(Atom variable);

void PrintVariable(Atom variable);

#endif	// VARIABLE_H
