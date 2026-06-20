/**
 * Main kernel routines
 */

#include "kernel/typedtuple.h"
#include "platform.h"


/**
 * Set up a default memory layout to enable paging and allocation.
 */
void SetupMemory(void);

void CleanupMemory(void);

/**
 * Initialize a new kernel, creating a blank "world"
 * with only the core predicates defined.
 * 
 * TODO: we also need methods to load a previously persisted state.
 */
void KernelInitialize(void);

/**
 * Shut down a kernel, removing all facts.
 * 
 * This is mainly used for debugging, to ensure deallocation works correctly.
 * In all other cases, we would simply flush the memory-mapped pages to disk
 * and exit.
 */
void KernelShutdown(void);

/**
 * High level method to assert a fact.
 * Adds a tuple to the corresponding relation table,
 * and adds an entry to the lookup table for each AT_ID actor.
 * The actors tuple may not contain variables.
 * To indicate an identifying fact, set idPosition to the 1-based position
 * of the identified atom.
 */
void AssertFact(Atom predicateForm, TypedTuple const * actors, uint8 idPosition);

/**
 * High level methd to retract a fact.
 * Removes the tuple from the corresponding relation table
 * and removes entries from the lookup table.
 * This function should always succeed, as facts can always
 * be retracted at any time.
 */
void RetractFact(Atom form, TypedTuple * actors);

/**
 * Remove all facts of a given form
 */
// void RetractAllFacts(Atom predicateForm);

/**
 * Permanent identifiers for core role names (satisfying (name @name))
 * 
 * NOTE: we now identify a role as a pair (predicate, role name).
 * An alternative would be to define a role as an ifact defined by
 * (role predicate-form name), so that we have a single ID
 */

#define ROLE_MULTISET				1
#define ROLE_ELEMENT				2
#define ROLE_MULTIPLE				3
#define	ROLE_PREDICATE_FORM			4
#define	ROLE_TERM_FORM				5
#define	ROLE_CLAUSE_FORM			6
#define	ROLE_CONJUNCTION_FORM		7

#define ROLE_LIST					8
#define ROLE_POSITION				9
#define ROLE_LENGTH					10

#define ROLE_PAIR					11		// required for formula
#define ROLE_LEFT					12
#define ROLE_RIGHT					13
#define	ROLE_QUOTE					14
#define	ROLE_QUOTED					15
#define	ROLE_STRING					16		// not really core language
#define ROLE_SIGN					17

#define N_CORE_ROLES				17


/**
 * Indexes the for core predicates forms into lookup tables (see kernel.c)
 * and into to the "core" B-tree services in ServiceRegistry.
 * For forms 1 and 2 these are also the hardcoded atom values.
 */

#define FORM_MULTISET_ELEMENT_MULTIPLE		1	// (multiset element multiple)
#define FORM_PREDICATE_FORM					2	// (predicate-form)
#define FORM_TERM_FORM						3	// (term-form predicate-form sign)
#define FORM_CLAUSE_FORM					4	// (clause-form)
#define FORM_CONJUNCTION_FORM				5	// (conjunction-form)
#define FORM_LIST_POSITION_ELEMENT			6	// (list position element)
#define FORM_LIST_LENGTH					7	// (list length)
#define FORM_PAIR_LEFT_RIGHT				8 	// (pair left right)
#define FORM_QUOTE_QUOTED					9	// (quote quoted)
#define FORM_STRING							10	// (string)

#define N_CORE_PREDICATES					10

/**
 * Lookup one of the "primitive" forms for core tables
 * Returns an AT_ID atom.
 */
Atom GetCorePredicateForm(index32 formId);

/**
 * Lookup a core role name. Returns an AT_NAME atom.
 */
Atom GetCoreRoleName(index32 roleId);

/**
 * Find the index in "canonical order" of a role in the
 * tuple of actors corresponding to the given core predicate form.
 * formId and roleId as as defined above.
 */
index8 CorePredicateRoleIndex(index32 formId, index32 roleId);
