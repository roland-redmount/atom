/**
 * The dictionary stores logic rules in clause form, as well as ifact rules.
 * An ifact rule is a term with one generator (*), such as (circle * radius r).
 */

#ifndef DICTIONARY_H
#define DICTIONARY_H

#include "kernel/typedtuple.h"
#include "lang/formula.h"
#include "btree/btree.h"
#include "util/ResizingArray.h"


/**
 * Setup an empty dictionary.
 */
void SetupDictionary(void);

/**
 * Restore the dictionary from the paging area upon restart.
 */
void RestoreDictionary(void);

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
 * Append each clause form holding the given term form to clauseForms, an array of
 * Atom, and return the number of clause forms appended. A clause form is appended once, even
 * if the term form occurs more than once in the clause form.
 */
size32 DictionaryFindClauseForms(Atom termForm, ResizingArray * clauseForms);

/**
 * CLAUDE: The form index records the term forms of each clause form with a rule in the
 * dictionary, and of each conjunction form with a relation; see AcquireRelation().
 * Each entry of the form index records that a term form occurs a number of times
 * (the multiple) in a clause form or a conjunction form.
 */
typedef struct s_FormIndexEntry {
	Atom termForm;
	Atom form;
	size32 multiple;
	bool isClause;
} FormIndexEntry;

/**
 * CLAUDE: Add a reference to the given conjunction form in the form index, or release one.
 * The relation registry calls these functions as a relation of the conjunction form
 * is created and removed.
 */
void DictionaryIndexConjunctionForm(Atom conjunctionForm);

void DictionaryUnindexConjunctionForm(Atom conjunctionForm);

typedef struct s_FormIndexIterator {
	Atom termForm;
	BTreeIterator btreeIterator;
} FormIndexIterator;

/**
 * CLAUDE: Iterate over the form index entries of the given term form.
 * A zero term form iterates over every entry. The form index must not be changed
 * during the iteration.
 */
void DictionaryIterateFormIndex(Atom termForm, FormIndexIterator * iterator);

bool FormIndexIteratorNext(FormIndexIterator * iterator);

FormIndexEntry const * FormIndexIteratorPeek(FormIndexIterator const * iterator);

void FormIndexIteratorEnd(FormIndexIterator * iterator);

/**
 * CLAUDE: Invalidate the services of every form depending on the given term forms, but not
 * the services of the given term forms. A form Q depends on a term form B if
 * 1) a clause form holds Q and the opposite of B, so that a rule for Q reads B as a body term,
 * 2) Q is a conjunction form holding B, or
 * 3) Q is the opposite of B, and B is closed; see CloseRelation().
 * Dependence is transitive: a form depending on Q depends on B too.
 *
 * A service compiled while B had no service has no operator reading B, so removing services
 * of B does not reach the service. This function is called when B may gain services: by
 * CreateTupleStore(), DictionaryAddClause(), DictionaryAddIFactRule() and CloseRelation().
 */
void InvalidateDependentServices(Atom const termForms[], size32 nTermForms);

/**
 * CLAUDE: Invalidate the services that a new relation of the given term form may affect,
 * before primitive services of the relation are created. Return true if the primitive
 * services of the relation must be marked stale; see ServiceMarkStale().
 * Called by CreateTupleStore() and IFactCreateReaderService().
 */
bool InvalidateServicesForNewRelation(Atom termForm);

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
/* CLAUDE: An ifact rule may also be a conjunction, such as (car * brand b & car * model m).
 * Each term of the conjunction must be a positive term with exactly one generator and at
 * least two roles. The terms must have distinct term forms, so (car * feature f & car * feature g)
 * is not an ifact rule. No variable is repeated anywhere in the conjunction. The rule has at
 * most RELATION_MAX_ARITY actors. */
bool IsIFactRule(Atom formula);

/**
 * CLAUDE: Write the term form of each term of an ifact rule to termForms[], in the order of
 * RelationFormGetTermForms(). Write the ID column of each term to idColumns[]. The ID column
 * of a term is the index of the generator among the actors of the term. Returns the number
 * of terms.
 */
size8 IFactRuleGetIDColumns(FormulaView const * ifactRule, Atom termForms[], index8 idColumns[]);


/**
 * Add an ifact rule to the dictionary. The formula must be a valid ifact rule;
 * see IsIFactRule(). Two ifact rules are equal if they have the same form and the
 * the generator of each term is in the same position, so variable names do not matter.
 * If an ifact rule is alreadyu in the dictionary; the existing rule is returned.
 * Adding an ifact rule invalidates any compiled services involving its form.
 */
FormulaView DictionaryAddIFactRule(Atom rule);

/**
 * Whether the dictionary holds an ifact rule equal to the given one.
 */
bool DictionaryContainsIFactRule(Atom rule);

/**
 * Whether the dictionary holds an ifact rule overlapping the given term form,
 * including a conjunction ifact rule with a term of the same form.
 * Used to decide whether a newly created primitive service must be marked stale.
 */
bool IFactRuleExistsForTermForm(Atom termForm);

/**
 * Remove a single ifact rule from the dictionary.
 * This invalidates compiled services involving the term form of the rule.
 */
void DictionaryRemoveIFactRule(FormulaView * ifactRule);

/**
 * Whether the dictionary holds an ifact rule of same form as the given form,
 * which may be a term or conjunction form. Unlike IFactRuleExistsForTermForm(),
 * the match must be exact.
 */
bool IFactRuleExistsForForm(Atom form);

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
/* CLAUDE: The form may be a conjunction form; see IsRelationForm(). */
void DictionaryIterateIFactRules(Atom form, DictionaryIterator * iterator);

bool DictionaryIteratorNext(DictionaryIterator * iterator);

TypedTuple const * DictionaryIteratorPeekActors(DictionaryIterator * iterator);

void DictionaryIteratorEnd(DictionaryIterator * iterator);

/**
 * Register functions referred to from persistent memory.
 */
void RegisterDictionaryFunctions(void);


#endif	// DICTIONARY_H
