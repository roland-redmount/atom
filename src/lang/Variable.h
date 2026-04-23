/**
 * Variables are used in queries to indicate "any" atom.
 * They are identified by a single letter, case-insensitive.
 * For context-free syntax, we prefix variables with _ 
 * When a variable occurs only once in a formula so that its
 * identity is irrelevant, an "anonymous" variable _ can be used.
 * 
 * Variables can specify a datum type; such typed variables
 * are only used internally by the VM to call untyped services
 * from typed (bytecode) services. When typed variables occur
 * in a formula, any two variables with the same name must have
 * the same type.
 * 
 * Currently, typed variables cannot be anonymous.
 * We should perhaps rework this so that a variable is anonymous
 * iff its name field is 0, but can still be typed and/or quoted.
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
 * Create a typed variable
 */
TypedAtom CreateTypedVariable(char name, byte type);

byte VariableGetType(Atom variable);

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
 * Determine if a variable matches an atom,
 * considering type if the variable is typed.
 */
bool VariableMatch(Atom variable, TypedAtom typedAtom);

/**
 * Handle quoted variables
 */
bool VariableIsQuoted(TypedAtom variable);
TypedAtom QuoteVariable(TypedAtom variable);
TypedAtom UnquoteVariable(TypedAtom variable);

void PrintVariable(TypedAtom variable);

#endif	// VARIABLE_H
