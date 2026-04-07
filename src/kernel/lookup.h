/**
 * Lookup maintains a table of all roles associatd with  DT_ID atoms (ONLY DT_ID atoms)
 * across all relation tables. Each lookup entry is a triple [atom form role-name]
 * since both the form and role name are needed to uniquely identify a role.
 * This is information is redundant with the corresponding relation table,
 * but serves to efficient locate roles from atoms, rather than scanning all
 * relations tables in the system. So lookup is basically an index.
 * 
 * When creating a fact with AssertFact(), entries for all DT_IF atoms
 * are added to the lookup table. For example, when creating the fact
 * 
 * (list @x element @e position @p)
 * 
 * where @x and @e are DT_ID atoms, AssertFact() creates the lookup entries
 * 
 * [@x (list element position) list]
 * [@e (list element position) element]
 * 
 * but no entry for @p as it was not a DT_ID atom (usually an integer).
 * 
 * An atom can be associated with a role multiple times, for example
 * atom @x in the facts
 * 
 * (list @x element @e position 1)
 * (list @x element @f position 2)
 * (list @x element @g position 3)
 * 
 * Lookup keeps a count of such identical associations.
 */

#include "btree/btree.h"
#include "lang/TypedAtom.h"


void InitializeLookup(void);
void FreeLookup(void);

size32 LookupTotalCount(void);

/**
 * Test whether an atom participates in a given role (DT_NAME).
 * If role == 0, the function returns true if the atom participates
 * in any role in the given predicate form.
 * If predicateForm == 0, the function returns true if the atom participates
 * in any role in any predicate form.
 * 
 * NOTE: these functions could take datums, as the atom types are always the same
 */
bool AtomHasRole(Atom atom, Atom predicateForm, Atom role);

/**
 * Add a lookup entry for an atom participating in a role.
 * This is called by AssertFact()
 */
void AtomAddRole(Atom atom, Atom predicateForm, Atom role);

/**
 * Remove a lookup entry for an atom participating in a role.
 * This is called by RetractFact()
 */
void AtomRemoveRole(Atom atom, Atom predicateForm, Atom role);

/**
 * Remove all roles for an DT_ID atom. This is used when removing a DT_ID atom.
 */
void LookupRemoveAllRoles(Atom atom);

/**
 * Add lookup entries for all actors in a predicate.
 */
void LookupAddPredicateRoles(Atom predicateForm, TypedAtom * actors);

/**
 *	Remove lookup entries for each actor in a predicate.
 */
void LookupRemovePredicateRoles(Atom predicateForm, TypedAtom * actors);

/**
 *	Remove lookup entries for all atoms acting in the given predicate form.
 */
void LookupRemoveAllPredicateRoles(Atom predicateForm);

 /**
 * A record associates any atom (key) to a role (value).
 * Both atom and role must be DT_ID atoms, so we store only their datums.
 * Because multiple facts may contain a given role, we count the number
 * of facts in the lookup record. For example, the facts
 * 
 *  (list @l position 1 element 'A')
 *  (list @l position 2 element 'B')
 *  (list @l position c element 'C')
 * 
 * will all match the lookup record with atom = @l, form = (list position element),
 * role = 'list', which will then have nFacts = 3.
 */
typedef struct s_LookupRecord {
	Atom atom;
	Atom predicateForm;
	Atom role;
	size32 nFacts;	// the number of facts that match this record
} LookupRecord;


/**
 * Lookup iterator. 
 */
typedef struct s_LookupIterator {
	BTreeIterator treeIterator;
	LookupRecord query;
} LookupIterator;

/**
 * Iterate over lookup records for a given atom.
 */
void LookupIterate(Atom atom, LookupIterator * iterator);

bool LookupIteratorHasRecord(LookupIterator const * iterator);

void LookupIteratorNext(LookupIterator * iterator);

Atom LookupIteratorGetForm(LookupIterator const * iterator);

Atom LookupIteratorGetRole(LookupIterator const * iterator);

void FreeLookupIterator(LookupIterator * iterator);

/**
 * For debugging
 */

void LookupDump(void);
