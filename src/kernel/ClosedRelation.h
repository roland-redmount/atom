/**
 * A closed relation applies the closed world assumption to a single relation: a tuple
 * that is not in a closed relation belongs to the opposite relation. The opposite relation has
 * the opposite term form, as given by TermFormCreateOppositeForm(). For example, if the
 * relation (even x) is closed, then the opposite relation (! even x) contains the tuple (3)
 * because (even 3) does not hold.
 *
 * A closed relation is identified by its term form, so that every typed relation of the form
 * is closed, including one whose Relation has not been created. The term form of a closed relation
 * is stored in the relation (closed-relation f), as in the fact (closed-relation [. even]).
 * This is used by the compiler to generate an all-input service for the opposite relation.
 *
 * AssertFact() rejects any assertion on a closed relation, since the negation of the fact is inferred.
 * To add a fact to a closed relation, the relation must first be opened, and then closed again.
 */

#ifndef CLOSEDRELATION_H
#define CLOSEDRELATION_H

#include "lang/Atom.h"


/**
 * Create the relation (closed-relation f), holding no tuples.
 * This function is called during bootstrapping; see KernelInitialize().
 */
void SetupClosedRelations(void);

/**
 * Restore the relation (closed-relation f) from the paging area upon restart.
 */
void RestoreClosedRelations(void);

/**
 * Drop the relation (closed-relation f). Every closed relation must have been opened.
 */
void TeardownClosedRelations(void);

/**
 * Return the term form (closed-relation)
 */
Atom GetClosedRelationForm(void);

// Result codes for CloseRelation()
#define CLOSE_OK					1	// the relation was closed
#define CLOSE_EXISTED				2	// the relation was closed already
#define CLOSE_NOT_TERM_FORM			3	// the atom is not a term form
#define CLOSE_OPPOSITE_CLOSED		4	// the opposite relation is closed
#define CLOSE_OPPOSITE_STORED		5	// the opposite relation has a TupleStore

/**
 * Close the (untyped) relation of the given term form, asserting the fact (closed-relation form).
 * The relation is not closed if the opposite relation is closed, or if a typed relation of
 * the opposite form has a TupleStore. Compiled services of the opposite relation are removed.
 * Returns one of the CLOSE_* result codes.
 */
int CloseRelation(Atom form);

/**
 * Reverse CloseRelation(), removing the fact (closed-relation form). Compiled services
 * of the opposite relation are removed. The relation of the given form must be closed.
 */
void OpenRelation(Atom form);

/**
 * Test whether the relation of the given form is closed. Unlike RelationIsClosed(),
 * this is false for the opposite relation of a closed relation.
 */
bool RelationIsClosedForm(Atom form);

/**
 * CLAUDE: Test whether the relation of the given form is either a closed relation,
 * or the opposite relation of a closed relation.
 */
bool RelationIsClosed(Atom form);

/**
 * CLAUDE: Test whether the relation of the given form is the opposite relation
 * of a closed relation, whose tuples are inferred from the closed relation.
 */
bool RelationIsClosedInferred(Atom form);


#endif	// CLOSEDRELATION_H
