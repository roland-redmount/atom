/**
 * A formula can be a predicate, term, clause or conjunction.
 * (NOTE: unclear if we ever need to represent conjuctions as formulas?)
 * It consists of a a form atom (AT_ID) and a tuple of actors.
 * A formula must be an Atom, since we must be able to represent reflected
 * formulas (see quote.c). Formulas are often short-lived objects, but
 * reflected formulas can be part of asserted facts.
 * 
 * We previously used a list atom instead of a typed tuple, but
 * this becomes complicated when relations (including (list position element))
 * are typed, so that all elements must be of the same type, while
 * the actors in a formula can be of different types, since they derive from
 * different roles (relation table columns). This is a fundamental mismatch.
 * We could solve this with a UNION across several (list position element) services,
 * but it gets quite complicated for such a fundamental data structure.
 * 
 * 
 * A possibility is to store formulas as tuples of relation tables, re-using
 * existing storage methods. However, this presents some problems:
 * (1) Formulas are not asserted facts, so they must somehow be marked as such in tables
 * (2) There is no stable reference (pointer) to a specific tuple in a relation table,
 *     so we must do a table search to inspect the actors of a formula. This could
 *     be optimized later with some form of tuple cache.
 * (3) Clauses may combine muliple terms across multiple tables. We would probably
 *     have to represent a clause as a table where each column is a predicate actor,
 *     rather than a single actors tuple. An advantage of this format is that "slicing"
 *     out actors for terms is simpler than with a single actors tuple.
 * 
 */


 /* 
 * NOTE: the mismatch between fully ordered actors tuple and partially ordered
 * multisets for the form is problematic when matching tuples to signatures.
 *  
 * It might be better to represent actors
 * using "multilists" where each position associates with a set of elements. For example
 * the form (+^2 =) would have a multilist where position 1 has two actors, so that
 * e.g. (+ 2 3 = 5) has actor multilist ({2 3} 5). Stable iteration order over sets
 * ensures that the representation {2 3} is always used, not {3 2}, so that actor
 * multilists are unique. For multiplicity over terms and clauses, we use nested multilists
 * in the same fashion as the form's nested multisets, e.g. the clause
 * 
 * (+ 2 3 = 5) | ( + 2 4 = 6) | odd 3
 * 
 * has form ((+^2 =)^2 | odd) and actor multilist ({ ({2 3} 5) ({2 4} 5) } 3).
 * 
 * For services, arguments bound to roles with multiplicity must be exchangable:
 * for example the service with signature
 * 
 * + 'x 'y = z
 * x: INT y:INT z:INT
 * 
 * must produce identical tuples for queries (+ 2 + 3 = _) and (+ 3 + 2 = _).
 * With the multilist representation, this is ensured since the arguments x y would
 * always be ordered as (2 3). Similarly, a service with signature
 * 
 * + 'x y = 'z
 * x: INT y:INT z:INT
 * 
 * (where y is now the output) will match (+ _ 3 = 5) if variables are always sorted
 * after integers, so the query multilist is ({3 _ } 5) and the service is ({x' y} 'z').
 * 
 * This is essentially the same as storing arguments
 * in a tuple/array but always keeping them sorted, so that x in the above service
 * always binds to the first argument (by iteration order) in the set for role '+'
 * 
 * Note that the multilist representaton does not solve the more general unification problem,
 * since we cannot have a total ordering on terms and clauses (??)
 * For example the signature
 * 
 * + x y = 'z & + x y = 'w
 * x: INT y:INT z:INT w:INT
 * 
 * has form (+^2 =)^2 and actors ({({x y} z) ({x y} w)}) ... 
 */

#ifndef FORMULA_H
#define FORMULA_H

#include "kernel/typedtuple.h"
#include "lang/TypedAtom.h"

typedef struct s_Formula {
	Atom form;
	TypedTuple * actors;	// or Atom atoms[], byte types[] ?
} Formula;


/**
 * Create a formula. The actors array is copied.
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
