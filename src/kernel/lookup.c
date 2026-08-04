
/**
 * We could implement lookup with a b-tree using atoms as keys,
 * but we need a b-tree implementation allowing duplicate keys,
 * or alternatively variable-sized items to fit multiple roles
 * per key. An easy option is to combine atom and role into an
 * item, and just have the compare function() look at the key
 * part only, as with ifacts.
 */

#include "btree/btree.h"
#include "kernel/lookup.h"
#include "kernel/multiset.h"
#include "lang/Atom.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "util/ResizingArray.h"


// TODO: move this to persistent memory
struct s_Lookup {
	BTree * btree;
	size32 nRolesTotal;
} lookup;


/**
 * Comparison function for lookup records, used for both queries and item ordering.
 * If the recordOrKey->predicateForm is 0, any item matching the atom is considered a match,
 * so a key with NULL role can be passed as first argument to match all roles
 * associated with a given atom.
 */
static int8 compareRecords(LookupRecord const * record, LookupRecord const * recordOrKey)
{
	int8 atomOrder = CompareAtoms(record->atom, recordOrKey->atom);
	if(atomOrder == 0) {
		if(recordOrKey->relation) {
			int8 relationOrder = ComparePointers(record->relation, recordOrKey->relation);
			if(relationOrder == 0) {
				if(recordOrKey->role.hash)
					return CompareAtoms(record->role, recordOrKey->role);
				else
					return 0;
			}
			else return relationOrder;
		}
		else
			return 0;
	}
	else
		return atomOrder;
}


static int8 btreeCompareRecords(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareRecords((LookupRecord *) item, (LookupRecord *) itemOrKey);
}


/**
 * Initialize storage for lookup
 */
void InitializeLookup(void)
{
	lookup.btree = BTreeCreate(
	    sizeof(LookupRecord),
	    btreeCompareRecords,
	    0	// free
	);
	lookup.nRolesTotal = 0;
}


void FreeLookup(void)
{
	ASSERT(BTreeNItems(lookup.btree) == 0)
	BTreeFree(lookup.btree);
}


size32 LookupTotalCount(void)
{
	return lookup.nRolesTotal;
}


bool AtomHasRole(Atom atom, RelationTable const * relation, Atom role)
{
	LookupRecord record = {
		.atom = atom,
		.relation = relation,
		.role = role
	};
	return BTreeContainsItem(lookup.btree, &record);
}


static void addRecord(LookupRecord * record)
{
	LookupRecord * existingRecord = BTreePeekItem(lookup.btree, record);
	if(existingRecord)
		existingRecord->nFacts++;
	else {
		record->nFacts = 1;
		ASSERT(BTreeInsert(lookup.btree, record) == BTREE_INSERTED)
	}
	lookup.nRolesTotal++;
}


void AtomAddRole(Atom atom, RelationTable const * relation, Atom role)
{
	LookupRecord record = {
		.atom = atom,
		.relation = relation,
		.role = role
	};
	// TODO: We should verify that the role actally exists in the given relation
	addRecord(&record);
}


void LookupAddPredicateRoles(RelationTable const * relation, Atom const * actors)
{
	// iterate over roles names in the predicate form
	// and add corresponding actors to lookup table
	LookupRecord record;
	record.relation = relation;

	MultisetIterator formIterator;
	MultisetIterate(relation->form, AT_NAME, &formIterator);
	index8 index = 0;
	while(MultisetIteratorNext(&formIterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&formIterator);
		for(index8 i = 0; i < em.multiple; i++, index++) {
			if(relation->atomTypes[index] != AT_ID)
				continue;
			record.atom = actors[index];
			record.role = em.element;
			addRecord(&record);
		}
	}
	MultisetIteratorEnd(&formIterator);
}


static void removeRecord(LookupRecord * record)
{
	LookupRecord * existingRecord = BTreePeekItem(lookup.btree, record);
	ASSERT(existingRecord)
	if(existingRecord->nFacts > 1)
		existingRecord->nFacts--;
	else {
		ASSERT(BTreeDelete(lookup.btree, record, 0) == BTREE_DELETED)
	}
	lookup.nRolesTotal--;
}


void AtomRemoveRole(Atom atom, RelationTable const * relation, Atom role)
{
	LookupRecord record = {
		.atom = atom,
		.relation = relation,
		.role = role
	};
	removeRecord(&record);
}


void LookupRemoveAllRoles(Atom atom)
{
	LookupRecord key = {
		.atom = atom,
		.relation = 0,
		.role = (Atom) {0}
	};
	LookupRecord record;
	// TODO: can we delete the item via the B-tree iterator more efficiently?
	while(BTreeGetItem(lookup.btree, &key, &record)) {
		lookup.nRolesTotal -= record.nFacts;
		BTreeDelete(lookup.btree, &record, 0);
		record.relation = 0;
		record.role = (Atom) {0};
	}
}


void LookupRemovePredicateRoles(RelationTable const * relation, Atom const * actors)
{
	LookupRecord record;
	record.relation = relation;

	MultisetIterator formIterator;
	MultisetIterate(relation->form, AT_NAME, &formIterator);
	index8 index = 0;
	while(MultisetIteratorNext(&formIterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&formIterator);
		for(index8 i = 0; i < em.multiple; i++) {
			if(relation->atomTypes[i] != AT_ID)
				continue;
			record.atom = actors[index];
			record.role = em.element;
			removeRecord(&record);
			index++;
		}
	}
	MultisetIteratorEnd(&formIterator);
}

/*
void LookupRemoveAllPredicateRoles(Atom predicateForm)
{
	// NOTE: this requires scanning the entire lookup table,
	// since it is indexed by atom, not predicate form.

	// Find all distinct atoms with a lookup entry for the given form.
	ResizingArray datumArray;
	CreateResizingArray(&datumArray, sizeof(Atom), 10);

	BTreeIterator iterator;
	BTreeIterate(&iterator, lookup.btree);
	Atom previousAtom = {0};
	while(BTreeIteratorNext(&iterator)) {
		LookupRecord const * record = BTreeIteratorPeekItem(&iterator);
		if(record->predicateForm.hash == predicateForm.hash) {
			// since lookup entries are ordered by atom,
			// we can skip any entry with the same atom as previous
			if(record->atom.hash != previousAtom.hash) {
				ResizingArrayAppend(&datumArray, &(record->atom));
				previousAtom = record->atom;
			}
		}
	}
	BTreeIteratorEnd(&iterator);

	// Free all lookup entries for discovered atoms
	Atom const * atoms = ResizingArrayGetMemory(&datumArray);
	size32 nAtoms = ResizingArrayNElements(&datumArray);
	for(index32 i = 0; i < nAtoms; i++)
		LookupRemoveAllRoles(atoms[i]);
		
	FreeResizingArray(&datumArray);
}
*/

void LookupIterate(Atom atom, LookupIterator * iterator)
{
	iterator->query = (LookupRecord) {
		.atom = atom,
		.relation = 0,
		.role = (Atom) {0}
	};
	BTreeIterate(&(iterator->treeIterator), lookup.btree);
}


/**
 * Advance the iterator to the next record matching the query, if any
 * If this function returns true, the tuple can be accessed by 
 * RelationBTreeIteratorGetTuple(). 
 */
bool LookupIteratorNext(LookupIterator * iterator)
{
	bool foundItem;
	if(BTreeIteratorBeforeFirst(&(iterator->treeIterator)))
		foundItem = BTreeIteratorSeek(&(iterator->treeIterator), &(iterator->query));
	else
		foundItem = BTreeIteratorNext(&(iterator->treeIterator));
	if(foundItem) {
		LookupRecord const * record = BTreeIteratorPeekItem(&(iterator->treeIterator));
		if(compareRecords(record, &(iterator->query)) == 0)
			return true;
	}
	return false;
}


RelationTable const * LookupIteratorGetRelation(LookupIterator const * iterator)
{
	LookupRecord const * record = BTreeIteratorPeekItem(&(iterator->treeIterator));
	return record->relation;
}


Atom LookupIteratorGetRole(LookupIterator const * iterator)
{
	LookupRecord const * record = BTreeIteratorPeekItem(&(iterator->treeIterator));
	return record->role;
}


void LookupIteratorEnd(LookupIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->treeIterator));
	SetMemory(iterator, sizeof(LookupIterator), 0);
}


RelationTable const * LookupFindRelation(Atom atom, Atom form, Atom role)
{
	RelationTable const * relation = 0;
	LookupIterator iterator;
	LookupIterate(atom, &iterator);
	while(LookupIteratorNext(&iterator)) {
		Atom currentRole = LookupIteratorGetRole(&iterator);
		RelationTable const * currentRelation = LookupIteratorGetRelation(&iterator);
		if((currentRole.hash == role.hash) && (currentRelation->form.hash == form.hash)) {
			ASSERT(relation == 0)	// ensure we have only 1 matching relation
			relation = currentRelation;
		}
	}
	LookupIteratorEnd(&iterator);
	return relation;
}


void LookupDump(void)
{
	PrintF("Lookup table %u records:\n", BTreeNItems(lookup.btree));
	BTreeIterator iterator;
	BTreeIterate(&iterator, lookup.btree);
	while(BTreeIteratorNext(&iterator)) {
		LookupRecord const * record = BTreeIteratorPeekItem(&iterator);
		IFactPrint(record->atom);
		PrintChar(' ');
		PrintPredicateForm(record->relation->form);
		PrintChar(' ');
		PrintName(record->role);
		PrintF(" %u\n", record->nFacts);
	}
	BTreeIteratorEnd(&iterator);
}
