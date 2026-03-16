/**
 * Main kernel routines
 */

#include "lang/Atom.h"
#include "platform.h"


/**
 * Set up a default memory layout to enable paging and allocation.
 */
void SetupMemory(void);

void CleanupMemory(void);

/**
 * Initialize a new kernel, creating a blank "world"
 * with only the core predicates defined.
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
 * and adds an entry to the lookup table for each DT_ID actor.
 */
void AssertFact(Atom form, Atom * actors);


/**
 * High level methd to retract a fact.
 * Removes the tuple from the corresponding relation table
 * and removes entries from the lookup table.
 * This function should always succeed, as facts can always
 * be retracted at any time.
 */
void RetractFact(Atom form, Atom * actors);


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

#define ROLE_LIST					8		// required for formula
#define ROLE_POSITION				9
#define ROLE_LENGTH					10

#define ROLE_PAIR					11		// required for formula
#define ROLE_LEFT					12
#define ROLE_RIGHT					13
#define	ROLE_FORMULA				14
#define	ROLE_QUOTE					15
#define	ROLE_QUOTED					16
#define	ROLE_STRING					17		// not really core language
#define ROLE_SIGN					18
#define ROLE_FORM					19
#define ROLE_ACTORS					20

#define ROLE_BYTECODE				21
#define ROLE_SIGNATURE				22
#define ROLE_PROGRAM				23
#define ROLE_PARAMETER				24
#define ROLE_REGISTERS				25
#define ROLE_CONSTANTS				26

#define N_CORE_ROLES				26


/**
 * Permanent identifiers for core predicates forms.
 * These are also used by TableRegistry for the corresponding
 * core relation tables.
 */

#define FORM_MULTISET_ELEMENT_MULTIPLE		1	// (multiset element multiple)
#define FORM_PREDICATE_FORM					2	// (predicate-form)
#define FORM_TERM_FORM						3	// (term-form predicate-form sign)
#define FORM_CLAUSE_FORM					4	// (clause-form)
#define FORM_CONJUNCTION_FORM				5	// (conjunction-form)
#define FORM_LIST_POSITION_ELEMENT			6	// (list position element)
#define FORM_LIST_LENGTH					7	// (list length)
#define FORM_PAIR_LEFT_RIGHT				8 	// (pair left right)
#define FORM_FORMULA_FORM_ACTORS			9	// (formula form actors)
#define FORM_QUOTE_QUOTED					10	// (quote quoted)
#define FORM_STRING							11	// (string)
#define FORM_BYTECODE_SIGNATURE				12	// (bytecode signature)
#define FORM_BYTECODE_PROGRAM				13	// (bytecode program)
#define FORM_BYTECODE_REGISTERS	    		14	// (bytecode registers)
#define FORM_BYTECODE_CONSTANTS	    		15	// (bytecode registers)

#define N_CORE_PREDICATES					15

/**
 * Lookup one of the "primitive" forms for core tables
 * Return a DT_ID.
 */
Atom GetCorePredicateForm(index32 formId);


/**
 * Lookup a core role name
 */
Atom GetCoreRoleName(index32 roleId);


/**
 * Find the index in "canonical order" of a role in the
 * tuple of actors corresponding to the given core predicate form.
 * formId and roleId as as defind above.
 */
index8 CorePredicateRoleIndex(index32 formId, index32 roleId);
