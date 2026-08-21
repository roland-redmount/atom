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
int AssertFact(Atom termForm, TypedTuple const * actors, RelationTableProvider const * provider);

/**
 * High level method to assert a formula, which is a fact if it is a term and a rule if it
 * is a clause. A fact is added with AssertFact() and a rule with DictionaryAddClause().
 *
 * A term holding a variable is no fact, and a clause is no rule unless it holds a variable
 * and at least two terms, so those are rejected rather than asserted. The result code says
 * which of these the formula turned out to be.
 */
int AssertFormula(Atom formula);

// Result codes for AssertFact() and AssertFormula()
#define ASSERT_OK				1	// a new fact or rule was created
#define ASSERT_EXISTED			2	// the fact or rule already existed, nothing changed
#define ASSERT_FAIL				3	// logical contradiction, nothing changed
#define ASSERT_FACT_VARIABLE	4	// a term holding a variable, which no fact may
#define ASSERT_RULE_GROUND		5	// a clause holding no variable, which every rule must
#define ASSERT_RULE_ONE_TERM	6	// a clause of one term, which says no more than the term
#define ASSERT_NOT_CLAUSE		7	// a formula that is neither a term nor a clause

/**
 * High level method to retract a fact. Removes the tuple from the corresponding
 * relation table and removes corresponding entries from the lookup table.
 * The actors tuple may not contain variables. This method cannot be used to
 * retract an identifying fact; this is done by ReleaseIFact().
 * This function should always succeed, as (non-identifying) facts can be retracted
 * at any time.
 */
void RetractFact(Atom termForm, TypedTuple const * actors);



#endif	// ASSERT_H
