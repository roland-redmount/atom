/**
 * High-level user interface for adding and removing facts in the knowledge base
 */

#ifndef ASSERT_H
#define ASSERT_H

#include "kernel/typedtuple.h"
#include "kernel/RelationTable.h"
#include "lang/formula.h"

/**
 * High level method to assert a fact. Adds a tuple to the corresponding
 * relation table, and adds an entry to the lookup table for each AT_ID actor.
 * The form is a term form, so a fact can be a negated predicate.
 * The actors tuple may not contain variables.
 * Creates a new relation table using the indicated storage provider if one
 * did not already exist; if provider = 0, RelationBTree is used by default.
 */
int AssertFact(FormulaView fact, RelationTableProvider const * provider);

/**
 * High level method to assert any formula.
 * 1) any formula that contains a generator (*) is an ifact,
 *    sent to CreateIFact()
 * 1) a single term without variables is a (ground) fact,
 *    sent to AssertFact()
 * 2) a clause with at least two terms and at least one variable is a rule,
 *    sent to DictionaryAddClause()
 */
int AssertFormula(Atom formula);

// Result codes for AssertFact() and AssertFormula()
#define ASSERT_OK					1	// a new fact or rule was created
#define ASSERT_EXISTED				2	// the fact or rule already existed, nothing changed
#define ASSERT_FAIL					3	// logical contradiction, nothing changed
#define ASSERT_TERM_VARIABLE		4	// a term containing variables cannot be a fact
#define ASSERT_CLAUSE_NO_VARIABLE	5	// a clause with no variables cannot be a rule
#define ASSERT_CLAUSE_ONE_TERM		6	// a clause of one term cannot be a rule
#define ASSERT_NOT_CLAUSE			7	// a conjunction
#define ASSERT_INVALID_IFACT		8	// a formula with generators that is not an ifact

/**
 * High level method to retract a fact. Removes the tuple from the corresponding
 * relation table and removes corresponding entries from the lookup table.
 * The actors tuple may not contain variables. This method cannot be used to
 * retract an identifying fact; this is done by ReleaseIFact().
 * This function should always succeed, as (non-identifying) facts can be retracted
 * at any time.
 */
void RetractFact(FormulaView fact);

/**
 * High level function to create an IFact from a formula containing
 * one of more generator (*) atoms. The caller obtains a reference to the IFact.
 */
Atom CreateIFact(FormulaView formula);


#endif	// ASSERT_H
