/**
 * The kernel provides the essential "core" relation tables and services
 */

#include "kernel/typedtuple.h"
#include "kernel/RelationTable.h"
#include "kernel/operator.h"
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
 * High level method to retract a fact.
 * The actors tuple may not contain variables.
 * Removes the tuple from the corresponding relation table
 * and removes corresponding entries from the lookup table.
 * This function should always succeed, as facts can always
 * be retracted at any time.
 */
void RetractFact(Atom form, TypedTuple * actors);

/**
 * Stable identifiers for core role names (satisfying (name @name))
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

#define ROLE_LIST					8       // was required for formula -- can be skipped 
#define ROLE_POSITION				9
#define ROLE_LENGTH					10

#define	ROLE_QUOTE					11
#define	ROLE_QUOTED					12

#define	ROLE_STRING					13		// not really core language
#define ROLE_SIGN					14

#define N_CORE_ROLES				14


/**
 * Stable identifiers the for core predicates forms
 */
#define FORM_MULTISET_ELEMENT_MULTIPLE		1	// (multiset element multiple)
#define FORM_PREDICATE_FORM					2	// (predicate-form)
#define FORM_TERM_FORM						3	// (term-form predicate-form sign)
#define FORM_CLAUSE_FORM					4	// (clause-form)
#define FORM_CONJUNCTION_FORM				5	// (conjunction-form)
#define FORM_LIST_POSITION_ELEMENT			6	// (list position element)
#define FORM_LIST_LENGTH					7	// (list length)
#define FORM_QUOTE_QUOTED					8	// (quote quoted)
#define FORM_STRING							9	// (string)

// #define FORM_PAIR_LEFT_RIGHT				X 	// (pair left right)

#define N_CORE_PREDICATES					9


/**
 * Stable identifiers the for core relations.
 * There may be > 1 relation per predicate, with distinct types.
 */
#define RELATION_MULTISET_NAME      		1	// (multiset:ID element:NAME multiple:UINT)
#define RELATION_PREDICATE_FORM				2	// (predicate-form:ID)
#define RELATION_MULTISET_ID         		3	// (multiset:ID element:ID multiple:INT)
#define RELATION_TERM_FORM					4	// (term-form:ID predicate-form:ID sign:UINT)
#define RELATION_CLAUSE_FORM				5	// (clause-form:ID)
#define RELATION_CONJUNCTION_FORM			6	// (conjunction-form:ID)
#define RELATION_LIST_LETTER        		7	// (list:ID position:UINT element:LETTER)
#define RELATION_LIST_ID  	 	     		8	// (list:ID position:UINT element:LETTER)
#define RELATION_LIST_LENGTH				9	// (list:ID length:UINT)
#define RELATION_QUOTE	    				10	// (quote:ID quoted:ID)
#define RELATION_STRING						11	// (string:ID)

// #define RELATION_PAIR_LEFT_RIGHT			X 	// (pair:ID left right)

#define N_CORE_RELATIONS					11

/**
 * Stable identifiers for a small set of core services.
 * There may be > 1 service per relation table.
 */
#define SERVICE_MULTISET_NAME				1	// (multiset <ID element >NAME multiple >UINT)
#define SERVICE_PREDICATE_FORM				2	// (predicate-form >ID)
#define SERVICE_MULTISET_ID					3	// (multiset <ID element >ID multiple >UINT)
#define SERVICE_MULTISET_ID_ALL				4	// (multiset >ID element >ID multiple >UINT)
#define SERVICE_TERM_FORM					5	// (term-form <ID predicate-form >ID)
#define SERVICE_LIST_LENGTH                 6	// (list <ID length >UINT)
#define SERVICE_LIST_LETTER					7	// (list <ID position >UINT element >LETTER)	
#define SERVICE_LIST_ID						8	// (list <ID position >UINT element >ID)	

#define N_CORE_SERVICES                     8

/**
 * Lookup one of the "primitive" forms for core tables
 * Returns an AT_ID atom.
 */
Atom GetCorePredicateForm(index32 formId);

/**
 * Set a tuple in the canonical order for the indicated core form,
 * given a tuple in "reference" order, according to coreFormRoleIds
 */
void CoreFormSetTuple(index32 formId, Atom const inputTuple[], Atom tuple[]);

/**
 * Set a byte array in the canonical order for the indicated core form,
 * given an array in "reference" order, according to coreFormRoleIds
 */
void CoreFormSetByteArray(index32 formId, byte const inputArray[], byte array[]);

/**
 * Lookup a core role name. Returns an AT_NAME atom.
 */
Atom GetCoreRoleName(index32 roleId);

RelationTable const * GetCoreRelationTable(index32 relationId);

/**
 * Return the operator of a core service, given a SERVICE_* id.
 */
Operator * GetCoreOperator(index32 serviceId);

/**
 * Get the atom types array for the given core predicate form/table
 */
byte const * GetCorePredicateAtomTypes(index32 formId);

/**
 * Find the index in "canonical order" of a role in the given core predicate form.
 */
index8 CorePredicateRoleIndex(index32 formId, index32 roleId);
