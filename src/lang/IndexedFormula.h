/**
 * A clause or conjunction with terms indexed for quick access.
 * See also lang/TermMultiset.h
 */

#ifndef INDEXEDFORMULA_H
#define INDEXEDFORMULA_H

#include "kernel/typedtuple.h"


typedef struct s_IndexedFormula {
	Atom form;
	TypedTuple * actors;
	// Index into the actors tuple of the first actor of each term, plus one entry for the end
	index8 * termActorsIndices;
} IndexedFormula;


/**
 * Create an IndexedFormula from a formula. The formula must be either
 * a clause or a conjunction. The given actors tuple is referred to, not copied,
 * so changes made to the IndexedFormula are also made to the actors tuple.
 */
IndexedFormula * CreateIndexedFormula(Atom form, TypedTuple * actors);

/**
 * Return the arity of term i
 */
size8 IndexedFormulaTermArity(IndexedFormula const * indexedFormula, index8 i);

/**
 * Return the element (typed atom) at index j in term i
*/
TypedAtom IndexedFormulaGetTermElement(IndexedFormula const * indexedFormula, index8 i, index8 j);

/**
 * Return the atom at index j in term i
*/
Atom IndexedFormulaGetTermAtom(IndexedFormula const * indexedFormula, index8 i, index8 j);

/**
 * Set the element (typed atom) at index j in term i
*/
void IndexedFormulaSetTermElement(
	IndexedFormula const * indexedFormula, index8 i, index8 j, TypedAtom element);

/**
 * Set the element atom at index j in term i
*/
void IndexedFormulaSetTermAtom(
	IndexedFormula const * indexedFormula, index8 i, index8 j, Atom atom);

/**
 * Return a view of the atoms array for term i
 */
Atom const * IndexedFormulaPeekAtoms(IndexedFormula const * indexedFormula, index8 i);


void FreeIndexedFormula(IndexedFormula const * indexedFormula);


#endif	// INDEXEDFORMULA_H
