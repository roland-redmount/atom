/**
 * A Formula stores a form and an actore tuple.
 * The form may be a predicate, term, clause or conjunction form.
 */

#ifndef FORMULA_H
#define FORMULA_H

#include "kernel/typedtuple.h"
#include "lang/TypedAtom.h"

typedef struct s_Formula {
	data64 hash;
	Atom form;
	TypedTuple * actors;	// or Atom atoms[], byte types[] ?
} Formula;


/**
 * Create a new AT_FORMULA. The actors array is copied.
 * The new formula holds a reference to the form atom. 
 */
Formula * CreateFormula(Atom form, TypedTuple const * actors);


Formula * CreateFormulaFromArray(Atom form, TypedAtom const * actors);


void FreeFormula(Formula * formula);

/**
 * Create a predicate from two arrays of role names (AT_NAME) and actors,
 * both of the same length nParts.
 */
Formula * CreatePredicate(Atom const * roleNames, TypedAtom * actors, size8 arity);

/**
 * Create a term from a predicate and sign
 */
Formula * CreateTerm(Formula const * predicate, bool sign);

/**
 * Find the term actor corresponding the given role and multiplicity m
 */
TypedAtom TermGetRoleActor(Atom termForm, TypedTuple const * termActors, const char * role, uint8 m);

/**
 * Create a clause from a list of term formulas, in any order.
 */
Formula * CreateClause(Formula const ** terms, size8 nTerms);

/**
 * Find the index into the list of terms corresponding the given clause form
 * of the m'th multiple of the given term form.
 */
index8 ClauseGetTermIndex(Atom clauseForm, Atom termForm, uint8 m);

/**
 * Find the index into a clauseForm actors tuple of the first actor in
 * the m'th multiple of the term form.
 */
index8 ClauseGetTermActorsIndex(Atom clauseForm, Atom termForm, uint8 m);

/**
 * Find the indices into a clauseForm tuple of the first actor in each term,
 * including multiples. The termIndices array must have at least as many elements
 * as the total number of terms + 1; the last element will be set to the total clause arity.
 */
void ClauseGetTermActorsIndices(Atom clauseForm, index8 * termActorsIndices);

/**
 * Create a conjunction from a list of terms, in any order.
 */
Formula * CreateConjunction(Formula const ** clauses, size8 nClauses);


/**
 * Formula type predicates.
 */
bool FormulaIsPredicate(Formula const * formula);

bool FormulaIsTerm(Formula const * formula);

bool FormulaIsClause(Formula const * formula);

bool FormulaIsConjunction(Formula const * formula);

uint8 FormulaArity(Formula const * formula);


/**
 * Return the list of actors.  REMOVE
 */
// Atom const * FormulaGetActors(Atom formula);

/**
 * Return the index of the given name in the corresponding form
 */
index32 FormulaRoleIndex(Formula const * formula, Atom roleName);

/**
 * Store a list of the unique formula variables into the provided array,
 * in left-to-right canonical order, and return the number of variables.
 * NOTE: currently not used
 */
// size8 FormulaUniqueVariables(Atom formula, TypedAtom * variables);

void PrintFormula(Formula const * formula);

void PrintFormActorsAsFormula(Atom form, TypedTuple const * actors);

/**
 * Compute hash of a formula from the form hash value and actors tuple
 */
data64 FormulaHashFormActors(data64 formHash, TypedTuple const * actors, size32 nActors, data64 initialHash);

#endif	// FORMULA_H
