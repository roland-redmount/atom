/**
 * Lookup maintains records of all roles associated with AT_ID atoms (ONLY AT_ID atoms)
 * across all relation tables. Each lookup entry is a tuple [atom relation term role],
 * which uniquely identifies the role played by the atom in a relation.
 * This is information is redundant with the corresponding relation table,
 * but serves to efficiently locate roles from atoms, rather than scanning all
 * relations tables in the system. So lookup is basically an index.
 * 
 * When creating a fact with AssertFact(), entries for all AT_ID atoms
 * are added to the lookup table. For example, when adding the tuple (in canonical order)
 * 
 * (list @list element "foo" position 42)
 * 
 * to a relation R with a single term t = (list element position), where @list and "foo"
 * are AT_ID atoms, we create the lookup entries
 * 
 * [@list R t list]
 * ["foo" R t element]
 * 
 * but no entry for (position 42) since the integer is not an AT_ID atom.
 * 
 * An atom can be associated with a role multiple times, for example
 * atom @x in the facts
 * 
 * (list @x element @e position 1)
 * (list @x element @f position 2)
 * (list @x element @g position 3)
 * 
 * Lookup stores a single record for these assocition, bu keeps a count of the number
 * of associations to the role.
 */

#include "btree/btree.h"
#include "kernel/Relation.h"
#include "kernel/typedtuple.h"


void InitializeLookup(void);
void FreeLookup(void);

size32 LookupTotalCount(void);

/**
 * Test whether an atom participates in a given role in the given term form
 * of the given relation. To enumerate roles for an atom, see LookupIterate().
 */
bool LookupHasEntry(Atom atom, Relation relation, Atom termForm, Atom role);

/**
 * Add a lookup entry for an atom participating in the given role
 * in the given term, in the given relation.
 */
void LookupAddRole(Atom atom, Relation relation, Atom termForm, Atom role);

/**
 * Remove a lookup entry for an atom participating in a role.
 * This is called by RetractFact()
 */
void LookupRemoveRole(Atom atom, Relation relation, Atom termForm, Atom role);

/**
 * Remove all roles for an AT_ID atom. This is used when removing a AT_ID atom.
 */
void LookupRemoveAllRoles(Atom atom);

/**
 * Add lookup entries for all actors in a fact from a given relation,
 * defined by an actor list corresponding to the relation's form.
 */
void LookupAddFactRoles(Relation relation, Atom const actors[]);

/**
 * Remove lookup entries for each actor in a fact, defined by an actor list
 * for a given relation.
 */
void LookupRemoveFactRoles(Relation relation, Atom const actors[]);

/**
 * Lookup the relation with the given term form where the given atom partipates
 * in the given role.
 * There must be at most one such relation, or the function will ASSERT,
 * If no such relation exists, returns the null relation.
 * 
 * This is used by list, multiset where there may be multiple relation tables
 * for lists with different element types, but all elements of one list are
 * in the same table.
 * 
 * TODO: this is not a good design. Figure out some better way to handle those
 * cases in list, multiset.
 */
Relation LookupFindRelation(Atom atom, Atom termForm, Atom role);

/**
 * Remove lookup entries for all atoms acting in the given predicate form.
 * NOTE: this function might not be needed
 */
// void LookupRemoveAllPredicateRoles(Atom predicateForm);

 /**
 * A record associates any atom (key) to a role (value).
 * Both the atom and role must be AT_ID atoms.
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
	Relation relation;
	Atom termForm;
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

bool LookupIteratorNext(LookupIterator * iterator);

Relation LookupIteratorGetRelation(LookupIterator const * iterator);

Atom LookupIteratorGetTermForm(LookupIterator const * iterator);

Atom LookupIteratorGetRole(LookupIterator const * iterator);

void LookupIteratorEnd(LookupIterator * iterator);

/**
 * For debugging
 */

void LookupDump(void);
