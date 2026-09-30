
#include "lang/formula.h"
#include "lang/SubstitutionList.h"
#include "lang/Variable.h"
#include "memory/allocator.h"


void SetupSubstitution(Substitution * subst, size8 capacity)
{
	// Do a single allocation for keys and values arrays
	subst->keys = Allocate(2 * sizeof(TypedAtom) * capacity);
	subst->values = subst->keys + capacity;
	subst->capacity = capacity;
	subst->nPairs = 0;
}

enum SubstituteMode {
	SUBSTITUTE_NORMAL = 1,
	SUBSTITUTE_QUOTED = 2
};

TypedAtom findValue(Substitution const * subst, TypedAtom key, enum SubstituteMode mode)
{
	// A key must be a variable or a parameter
	ASSERT((key.type == AT_VARIABLE) || (key.type == AT_PARAMETER))
	if(mode == SUBSTITUTE_QUOTED) {
		if(VariableIsQuoted(key.atom)) {
			// A quoted variable ^x should be matched to an unquoted variable x
			// in the substitution list, so unquote it and recurse
			TypedAtom unquotedVariable = CreateTypedAtom(AT_VARIABLE, UnquoteVariable(key.atom));
			TypedAtom substValue = findValue(subst, unquotedVariable, SUBSTITUTE_NORMAL);
			if(substValue.type == AT_VARIABLE) {
				// The substitution list can't yield a quoted variable. I think.
				ASSERT(!VariableIsQuoted(substValue.atom))
				// A mapping x -> y in the substitution list here means replace ^x with ^y
				return CreateTypedAtom(AT_VARIABLE, QuoteVariable(substValue.atom));
			}
			else
				return substValue;
		}
		// Else we have a reflection-local variable, no match
	}
	else {
		for(index8 i = 0; i < subst->nPairs; i++) {
			if(SameTypedAtoms(subst->keys[i], key))
				return subst->values[i];
		}
	}
	// variable not found
	return invalidAtom;	
}


TypedAtom SubstitutionFindValue(Substitution const * subst, TypedAtom key)
{
	return findValue(subst, key, SUBSTITUTE_NORMAL);
}


void SubstitutionAdd(Substitution * subst, TypedAtom key, TypedAtom value)
{
	ASSERT((key.type == AT_VARIABLE) || (key.type == AT_PARAMETER))
	bool found = false;
	for(index32 i = 0; i < subst->nPairs; i++) {
		if(SameTypedAtoms(subst->values[i], key)) {
			// Replace any x -> key with key -> value
			// NOTE: this may lead to duplicated pairs in the Substitution,
			// but this has no effect on SubstituteTuple()
			subst->values[i] = value;
		}
		if(SameTypedAtoms(subst->keys[i], key)) {
			// key found, set its value
			subst->values[i] = value;
			found = true;
 		}
	}
	if(!found) {
		// else add new key-value pair
		index32 i = subst->nPairs;
		ASSERT(i < subst->capacity)
		subst->keys[i] = key;
		subst->values[i] = value;
		subst->nPairs++;
	}
}


void substituteTuple(
	Substitution const * subst, TypedTuple const * source, TypedTuple * destination, enum SubstituteMode mode)
{
	ASSERT(source->nAtoms == destination->nAtoms)
	for(index8 i = 0; i < source->nAtoms; i++) {
		TypedAtom sourceValue = TypedTupleGetElement(source, i);
		TypedAtom substValue = invalidAtom;
		// A generated reflection is the one value this function holds a reference to.
		// Every other value is read from the source tuple or the substitution list.
		bool substValueIsOwned = false;
		if(sourceValue.type == AT_FORMULA && mode == SUBSTITUTE_NORMAL) {
			FormulaView reflectedFormula = FormulaGetView(sourceValue.atom);
			// We substitute inside reflections only one level deep, since
			// a nested reflection cannot share variables with the top scope.
			// Substitute quoted variables within the actors tuple of the reflected formula
			TypedTuple * substReflectionTuple = CreateTypedTuple(reflectedFormula.actors->nAtoms);
			substituteTuple(subst, reflectedFormula.actors, substReflectionTuple, SUBSTITUTE_QUOTED);
			// Generate a new reflected formula
			substValue = CreateTypedAtom(
				AT_FORMULA, CreateFormula(reflectedFormula.form, substReflectionTuple));
			FreeTypedTuple(substReflectionTuple);
			substValueIsOwned = true;
		}
		else if((sourceValue.type == AT_VARIABLE)
			|| ((sourceValue.type == AT_PARAMETER) && (mode == SUBSTITUTE_NORMAL)))
			substValue = findValue(subst, sourceValue, mode);

		if(!substValue.type)
			substValue = sourceValue;

		// The destination tuple takes a reference of its own, so hand over
		// the reference to a generated reflection rather than keeping it
		TypedTupleSetElement(destination, i, substValue);
		if(substValueIsOwned)
			ReleaseTypedAtom(substValue);
	}
}


void SubstituteTuple(Substitution const * subst, TypedTuple const * source, TypedTuple * destination)
{
	substituteTuple(subst, source, destination, SUBSTITUTE_NORMAL);
}


void FreeSubstitution(Substitution const * subst)
{
	// free atom arrays
	Free(subst->keys);
}


void PrintSubstitution(Substitution * subst)
{
	PrintChar('{');
	for(index8 i = 0; i < subst->nPairs; i++) {
		PrintTypedAtom(subst->keys[i]);
		PrintCString(" -> ");
		PrintTypedAtom(subst->values[i]);
		PrintChar(' ');
	}
	PrintChar('}');
}
