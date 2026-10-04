/**
 * The dictionary stores logic rules in clause form, as well as ifact rules.
 * An ifact rule is a term with one generator (*), such as (circle * radius r).
 */

#ifndef DICTIONARY_H
#define DICTIONARY_H

#include "kernel/typedtuple.h"
#include "lang/formula.h"
#include "btree/btree.h"


/**
 * Setup an empty dictionary.
 */
void SetupDictionary(void);

void TeardownDictionary(void);

/**
 * Add a clause (formula) to the dictionary.
 * A clause already in the dictionary is not added again: nothing changes and the entry
 * already there is returned. A caller that must know whether a given rule is new must
 * call DictionaryContainsClause().
 * Adding a clause invalidates compiled services involving any term in that clause.
 * 
 * TODO: clauses are currently compared for equality, so it is possible to add multiple
 * clauses that differt only in variable names, such as (foo x | ! bar x) and (foo y | ! bar y).
 * This should be avoided, as it gives the compiler duplicate rules.
 */
FormulaView DictionaryAddClause(Atom clause);

/**
 * Whether the dictionary already holds the given clause.
 */
bool DictionaryContainsClause(Atom clause);

/**
 * Whether any clause form holds the given term form, i.e. some rule derives the term form.
 * Used to decide whether a newly created primitive service must be marked stale.
 */
bool ClauseFormExistsForTermForm(Atom termForm);

/**
 * Parse a string into a clause (formula) and call DictionaryAddClause()
 */
FormulaView DictionaryAddClauseFromCString(const char * clauseString);

/**
 * Remove a single clause from the dictionary
 * This invalidates compiled services involving any term in the clause.
 * 
 * TODO: we probably need some ownership system for rules.
 */
void DictionaryRemoveClause(FormulaView * clause);

/**
 * Test if the formula is a valid ifact rule: a positive term with exactly one
 * generator (*), where every other actor is a variable and no variable is repeated.
 * The term must have at least two roles, since the IFACT operator needs an input.
 */
bool IsIFactRule(Atom formula);

/**
 * Index of the generator in the actors of an ifact rule.
 */
index8 IFactRuleFindGeneratorIndex(TypedTuple const * actors);

/**
 * Add an ifact rule to the dictionary. The formula must be a valid ifact rule;
 * see IsIFactRule(). Two ifact rules are equal if they have the same term form and the
 * generator in the same role, so variable names do not matter. An ifact rule equal to
 * one already in the dictionary is not added again, and the entry already there is
 * returned. Adding an ifact rule invalidates compiled services involving its term form.
 */
FormulaView DictionaryAddIFactRule(Atom rule);

/**
 * Whether the dictionary holds an ifact rule equal to the given one.
 */
bool DictionaryContainsIFactRule(Atom rule);

/**
 * Whether the dictionary holds an ifact rule of the given term form.
 * Used to decide whether a newly created primitive service must be marked stale.
 */
bool IFactRuleExistsForTermForm(Atom termForm);

/**
 * Remove a single ifact rule from the dictionary.
 * This invalidates compiled services involving the term form of the rule.
 */
void DictionaryRemoveIFactRule(FormulaView * ifactRule);

/**
 * Remove all rules, both clauses and ifact rules, from the dictionary.
 * This invalidates all compiled services. Used for testing only. 
 */
void DictionaryRemoveAll(void);


typedef struct {
	FormulaView key;
	BTreeIterator btreeIterator;
} DictionaryIterator;

/**
 * Iterate over clauses of a given form
 */
void DictionaryIterateClauses(Atom clauseForm, DictionaryIterator * iterator);

/**
 * Iterate over the ifact rules of a given term form.
 */
void DictionaryIterateIFactRules(Atom termForm, DictionaryIterator * iterator);

bool DictionaryIteratorNext(DictionaryIterator * iterator);

TypedTuple const * DictionaryIteratorPeekActors(DictionaryIterator * iterator);

void DictionaryIteratorEnd(DictionaryIterator * iterator);



#endif	// DICTIONARY_H
