/**
 * A formula is defined by a fact
 * 
 * (formula @formula form @form actors @actors)
 * 
 * where @actors is a list matching the iteration ("canonical") order of the form.
 * 
 * NOTE: the mismatch between fully ordered list of actors and partially ordered
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
 * For bytecode services, arguments bound to roles with multiplicity must be exchangable:
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
 * in a tuple/array but always keeping them sorted, so that x in the above bytecode
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
 * 
 */

#ifndef FORMULA_H
#define FORMULA_H

#include "lang/TypedAtom.h"


/**
 * Create a formula from a form and an actors list (DT_LIST).
 * The order of actors must correspond to the iteration order of the form.
 */
Datum CreateFormula(Datum form, Datum actorsList);

Datum CreateFormulaFromArray(Datum form, TypedAtom * actors);

/**
 * Convenience functions for creating formulas
 */
Datum CreatePredicate(Datum const * roles, TypedAtom * actors, size8 nParts);
Datum CreateTerm(Datum predicate, bool negated);

/**
 * Create a clause from a list of terms, in any order.
 */
Datum CreateClause(Datum const * terms, size8 nTerms);

/**
 * Test if the atom is a formula
 */
bool IsFormula(Datum atom);

/**
 * Formula type predicates. These assume the atom is indeed a formula.
 */
bool FormulaIsPredicate(Datum formula);
bool FormulaIsTerm(Datum formula);
bool FormulaIsClause(Datum formula);
bool FormulaIsConjunction(Datum formula);

uint8 FormulaArity(Datum formula);

Datum FormulaGetForm(Datum formula);

/**
 * Return the list of actors
 */
Datum FormulaGetActors(Datum formula);

/**
 * Return the index of the given name in the corresponding form
 */
index32 FormulaRoleIndex(Datum formula, Datum name);

/**
 * Store a list of the unique formula variables into the provided array,
 * in left-to-right canonical order, and return the number of variables.
 */
size8 FormulaUniqueVariables(Datum formula, TypedAtom * variables);

void PrintFormula(Datum formula);

/**
 * Compute hash of a formula from the form hash value and actors tuple
 */
data64 FormulaHashFormActors(data64 formHash, TypedAtom const * actors, size32 nActors, data64 initialHash);

#endif	// FORMULA_H
