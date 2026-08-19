/**
 * High-level user interface for adding and removing facts in the knowledge base
 */

#ifndef ASSERT_H
#define ASSERT_H

#include "kernel/typedtuple.h"


/**
 * High level method to assert a fact. Adds a tuple to the corresponding
 * relation table, and adds an entry to the lookup table for each AT_ID actor.
 * The actors tuple may not contain variables.
 * To create an identifying fact, set idPosition to the 1-based position
 * of the identified atom; else set idPosition = 0.
 * The form is a term form, so a fact can be a negated predicate.
 */
void AssertFact(Atom termForm, TypedTuple const * actors, uint8 idPosition);

/**
 * High level method to retract a fact. Removes the tuple from the corresponding
 * relation table and removes corresponding entries from the lookup table.
 * The actors tuple may not contain variables. This method may not be used to
 * retract an identifying fact; this is done by ReleaseIFact().
 * This function should always succeed, as (non-identifying) facts can be retracted
 * at any time.
 */
void RetractFact(Atom termForm, TypedTuple * actors);


#endif	// ASSERT_H
