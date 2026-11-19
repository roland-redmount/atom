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
 * Creates a new relation table using the indicated storage provider if one
 * did not already exist; if provider = 0, RelationBTree is used by default.
 * Returns an error code (see below) if the formula is not a valid fact,
 * or if the assertion fails.
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

// Result codes for AssertFact(), AssertRule() and CreateGenerator()
#define ASSERT_OK					1	// a new fact or rule was created
#define ASSERT_EXISTED				2	// the fact or rule already existed, nothing changed
#define ASSERT_CONTRADICTION		3	// logical contradiction, nothing changed
#define ASSERT_TERM_VARIABLE		4	// a term containing variables cannot be a fact
#define ASSERT_CLAUSE_NO_VARIABLE	5	// a clause with no variables cannot be a rule
#define ASSERT_CLAUSE_ONE_TERM		6	// a clause of one term cannot be a rule
#define ASSERT_NOT_CLAUSE			7	// a conjunction
#define ASSERT_NOT_GENERATOR		8	// a formula that is not a valid generator
#define ASSERT_NOT_WRITABLE			9	// the specified relation is not writable
#define ASSERT_NOT_CLOSABLE			10	// the relation cannot be closed; see CloseRelation()
#define ASSERT_RESERVED_FORM		11	// a rule cannot contain the term form (closed-relation)
#define ASSERT_CLAUSE_WITH_NOVUM	12	// a rule cannot contain a novum (*)
#define ASSERT_NOT_TERM				13	// a fact must be a term
#define ASSERT_TERM_WITH_NOVUM		14	// a term containing a novum (*) cannot be a fact

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
 * Remove a rule from the dictionary; the counterpart of AssertRule().
 * See DictionaryRemoveClause().
 *
 * NOTE: A clause rule must have the same variable names as when asserted; see DictionaryAddClause().
 */
uint8 DropRule(Atom clause);

/**
 * Remove a generator from the dictionary; the counterpart of CreateGenerator().
 * See DictionaryRemoveGenerator().
 */
uint8 DropGenerator(Atom formula);

// Result codes for RetractFact(), DropRule() and DropGenerator()
#define RETRACT_OK					1	// a fact, rule or generator was retracted
#define RETRACT_NOT_FOUND			2	// the fact or rule is not in the knowledgebase
#define RETRACT_NOT_ASSERTED		3	// the fact is not explicitly asserted (ifact or computed fact)
#define RETRACT_TERM_VARIABLE		4	// a term containing variables is not a fact
#define RETRACT_INVALID_GENERATOR	5	// a formula with novums that is not a generator
#define RETRACT_NOT_CLAUSE			6	// the formula is not a clause
#define RETRACT_NOT_TERM			7	// a fact must be a term
#define RETRACT_TERM_WITH_NOVUM		8	// a term containing a novum (*) is not a fact

/**
 * High level function to create an IFact from a formula containing
 * one of more novum (*) atoms. The caller obtains a reference to the IFact.
 */
/* CLAUDE: Returns a zero atom if the formula is not a valid ifact, or if a relation of
   the formula has a TupleStore; see IFactSetupRelation(). */
Atom CreateIFact(FormulaView formula);


#endif	// ASSERT_H
