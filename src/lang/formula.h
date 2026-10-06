/**
 * A formula is a form together with the actors filling its roles.
 * The form may be a predicate, term, clause or conjunction form.
 *
 * A formula is an atom of type AT_FORMULA, identified by a hash of its form
 * and actors. Formulas are unique: creating a formula that already exists
 * yields the same atom and adds a reference to it, as with AT_NAME; see name.h.
 * The formula registry owns the actors tuple of every formula it stores.
 */

#ifndef FORMULA_H
#define FORMULA_H

#include "kernel/typedtuple.h"
#include "lang/TypedAtom.h"


// The arity of a formula is a size8
#define FORMULA_MAX_ARITY	255

/**
 * A view of the form and actors of a formula, as returned by FormulaGetView().
 * A view owns nothing, and is only valid while the caller holds a reference
 * to the formula it was taken from.
 */
typedef struct s_FormulaView {
	Atom form;
	TypedTuple const * actors;
} FormulaView;


/**
 * Set up and tear down the formula registry.
 * FreeFormulaStorage() requires every formula to have been released.
 */
void InitializeFormulaStorage(void);

/**
 * Restore the formula registry from the paging area upon restart.
 */
void RestoreFormulaStorage(void);

void FreeFormulaStorage(void);

/**
 * Create a formula from a form and an actors tuple, which is copied.
 * The formula holds a reference to the form atom and to each actor.
 */
Atom CreateFormula(Atom form, TypedTuple const * actors);

/**
 * Create a formula from a form and an array of actors, whose length must be
 * the arity of the form.
 */
Atom CreateFormulaFromArray(Atom form, TypedAtom const actors[]);

/**
 * Add a reference to a formula.
 */
void AcquireFormula(Atom formula);

/**
 * Remove a reference to a formula, and remove the formula itself once its
 * last reference is gone.
 */
void ReleaseFormula(Atom formula);

/**
 * The form of a formula.
 */
Atom FormulaGetForm(Atom formula);

/**
 * The actors tuple of a formula. The tuple belongs to the formula registry and
 * is shared by every holder of the formula, so it must not be written to. It
 * stays valid while the caller holds a reference to the formula, including
 * across the creation and release of other formulas.
 */
TypedTuple const * FormulaGetActors(Atom formula);

/**
 * The form and actors of a formula, retrieved in a single lookup.
 */
FormulaView FormulaGetView(Atom formula);

/**
 * The number of formulas in the registry, and the number of references to them.
 */
size32 NumberOfFormulas(void);

uint32 FormulaTotalReferenceCount(void);

/**
 * Print every formula in the registry with its reference count.
 */
void FormulaDump(void);

/**
 * Create a predicate from two arrays of role names (AT_NAME) and actors,
 * both of the same length nParts.
 */
Atom CreatePredicate(Atom const roleNames[], TypedAtom actors[], size8 arity);

/**
 * Create a term from a predicate and sign
 */
Atom CreateTerm(Atom predicate, bool sign);

/**
 * Find the term actor corresponding the given role and multiplicity m
 * NOTE: this is currently only used by test_compiler.c
 */
Atom TermGetRoleActor(Atom termForm, Atom const termActors[], const char * role, uint8 m);

/**
 * Create a clause from a list of term formulas, in any order.
 * No term may be repeated.
 *
 * Unless termOrder is 0, the order of the terms in the clause is written to
 * termOrder, which must have room for nTerms elements: term termOrder[i] of the list
 * is the term at index i of the clause. See MultisetIterationOrder().
 */
Atom CreateClause(Atom const terms[], size8 nTerms, index8 termOrder[]);

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
 * Create a conjunction from a list of terms, in any order.
 * No term may be repeated.
 *
 * Unless termOrder is 0, the order of the terms is written to termOrder,
 * as for CreateClause().
 */
Atom CreateConjunction(Atom const terms[], size8 nTerms, index8 termOrder[]);


/**
 * Formula type predicates.
 */
bool FormulaIsPredicate(Atom formula);

bool FormulaIsTerm(Atom formula);

bool FormulaIsClause(Atom formula);

bool FormulaIsConjunction(Atom formula);

uint8 FormulaArity(Atom formula);

/**
 * Return the index of the given name in the corresponding form
 */
index32 FormulaRoleIndex(Atom formula, Atom roleName);

/**
 * Print a formula
 */
void PrintFormula(Atom formula);

void PrintFormulaView(FormulaView formulaView);

/**
 * The order in which the terms and the roles of a formula were entered, as
 * written by ParseFormula(). A formula stores its terms, roles and actors in the
 * order of its form instead; see MultisetIterationOrder().
 *
 * termOrder[i] is the entered index of the term at index i of the form. A predicate
 * or term form has a single term. The role order of each term is stored in roleOrder
 * at the same indices as the actors of the term in the actors tuple. So for a term whose
 * first actor has index a, roleOrder[a + i] is the entered index (within the term) of
 * the role at index i of the term.
 */
typedef struct s_FormOrdering {
	size8 nTerms;
	index8 termOrder[FORMULA_MAX_ARITY];
	index8 roleOrder[FORMULA_MAX_ARITY];
} FormOrdering;

/**
 * Print a (form, actors) pair a formula. Unless ordering is 0, the terms
 * and roles are printed in the order given by the FormOrdering, rather than in
 * the order of the form.
 */
void PrintFormActorsAsFormula(Atom form, TypedTuple const * actors, FormOrdering const * ordering);

/**
 * Return te number of actors of the given form.
 */
size8 FormArity(Atom form);

/**
 * Print the given form, without actors.
 */
void PrintForm(Atom form);

/**
 * Register functions referred to from persistent memory.
 */
void RegisterFormulaFunctions(void);


#endif	// FORMULA_H
