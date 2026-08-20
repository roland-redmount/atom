/**
 * High-level user interface for adding and removing facts in the knowledge base
 */

#ifndef ASSERT_H
#define ASSERT_H

#include "kernel/typedtuple.h"
#include "kernel/RelationTable.h"

/**
 * High level method to assert a fact. Adds a tuple to the corresponding
 * relation table, and adds an entry to the lookup table for each AT_ID actor.
 * The form is a term form, so a fact can be a negated predicate.
 * The actors tuple may not contain variables.
 * Creates a new relation table using the indicated storage provider if one
 * did not already exist; if provider = 0, RelationBTree is used by default.
 */
int AssertFact(Atom termForm, TypedTuple const * actors, RelationTableProvider const * provider);

// Result codes for AssertFact
#define ASSERT_OK			1	// a new fact was created
#define ASSERT_EXISTED		2	// fact already existed, nothing changed
#define ASSERT_FAIL			3	// logical contradiction, nothing changed

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
