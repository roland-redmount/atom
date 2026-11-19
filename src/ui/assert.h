/**
 * High-level user interface for adding and removing facts in the knowledge base
 */

#ifndef ASSERT_H
#define ASSERT_H

#include "kernel/typedtuple.h"
#include "kernel/TypedRelation.h"
#include "lang/formula.h"

/**
 * High level method to assert a fact. Adds a tuple to the corresponding
 * relation table, and adds an entry to the lookup table for each AT_ID actor.
 * The form is a term form, so a fact can be a negated predicate.
 * The actors tuple may not contain variables.
 * Creates a new relation table using the indicated storage provider if one
 * did not already exist; if provider = 0, RelationBTree is used by default.
 */
uint8 AssertFact(FormulaView fact, StorageProvider const * provider);

/**
 * Add a rule to the dictionary. The formula must be a clause of at least two terms
 * with at least one variable, and may not contain a novum (*).
 * See DictionaryAddClause().
 */
uint8 AssertRule(Atom clause);

/**
 * Add a generator to the dictionary; see IsGenerator() and DictionaryAddGenerator().
 * The formula may be a term or a conjunction.
 */
uint8 CreateGenerator(Atom formula);

/**
 * High level method to assert any formula.
 * 1) a formula containing a novum (*) is a generator, sent to
 *    CreateGenerator()
 * 2) a single term without variables is a (ground) fact,
 *    sent to AssertFact()
 * 3) a clause with at least two terms and at least one variable is a rule,
 *    sent to AssertRule()
 */
uint8 AssertFormula(Atom formula);

// Result codes for AssertFact(), AssertRule(), CreateGenerator() and AssertFormula()
#define ASSERT_OK					1	// a new fact or rule was created
#define ASSERT_EXISTED				2	// the fact or rule already existed, nothing changed
#define ASSERT_CONTRADICTION		3	// logical contradiction, nothing changed
#define ASSERT_TERM_VARIABLE		4	// a term containing variables cannot be a fact
#define ASSERT_CLAUSE_NO_VARIABLE	5	// a clause with no variables cannot be a rule
#define ASSERT_CLAUSE_ONE_TERM		6	// a clause of one term cannot be a rule
#define ASSERT_NOT_CLAUSE			7	// a conjunction
#define ASSERT_INVALID_IFACT		8	// a formula with novums that is not an ifact
#define ASSERT_NOT_WRITABLE			9	// the specified relation is not writable
#define ASSERT_NOT_CLOSABLE			10	// CLAUDE: the relation cannot be closed; see CloseRelation()
#define ASSERT_RESERVED_FORM		11	// CLAUDE: a rule cannot derive (closed-relation f)
#define ASSERT_RULE_NOVUM			12	// CLAUDE: a rule cannot contain a novum (*)
#define ASSERT_NOT_TERM				13	// CLAUDE: a fact must be a term without a novum (*)

/**
 * High level method to retract a fact. Removes the tuple from the corresponding
 * relation table and removes corresponding entries from the lookup table.
 * The fact may not contain variables. This method cannot be used to
 * retract an identifying fact; see ReleaseIFact().
 * This function returns RETRACT_OK, RETRACT_NOT_ASSERTED or RETRACT_NOT_FOUND
 * if the fact was asserted, entailed by a rule, or not entailed at all, respectively.
 */
uint8 RetractFact(FormulaView fact);

/**
 * High level method to retract any formula; the counterpart of AssertFormula().
 * A generator is removed by DictionaryRemoveGenerator(), a fact by RetractFact(),
 * and a clause by DictionaryRemoveClause().
 * 
 * NOTE: A clause ruel must have the same variable names as when asserted; see DictionaryAddClause().
 */
uint8 RetractFormula(Atom formula);

// Result codes for RetractFact() and RetractFormula()
#define RETRACT_OK					1	// a fact or rule was retracted
#define RETRACT_IFACT_OK			2	// a generator was retracted
#define RETRACT_NOT_FOUND			3	// the fact or rule is not in the knowledgebase
#define RETRACT_NOT_ASSERTED		4	// the fact is not explicitly asserted (ifact or computed fact)
#define RETRACT_TERM_VARIABLE		5	// a term containing variables is not a fact
#define RETRACT_INVALID_GENERATOR	6	// a formula with novums that is not a generator
#define RETRACT_NOT_CLAUSE			7	// a conjunction

/**
 * High level function to create an IFact from a formula containing
 * one of more novum (*) atoms. The caller obtains a reference to the IFact.
 */
/* CLAUDE: Returns a zero atom if the formula is not a valid ifact, or if a relation of
   the formula has a TupleStore; see IFactSetupRelation(). */
Atom CreateIFact(FormulaView formula);


#endif	// ASSERT_H
