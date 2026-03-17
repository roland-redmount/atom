
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
#include "lang/Datum.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "util/ResizingArray.h"


// TODO: move this to persistent memory
struct s_Lookup {
	BTree * btree;
	size32 nRolesTotal;
} lookup;


/**
 * Comparison function for btree, used for both queries and item ordering. For queries,
 * record1 is the query key.
 * 
 * TODO: if the record1->role is NULL, any item matching the atom is considered a match,
 * so a key with NULL role can be passed as first argument to match all roles
 * associated with a given atom.
 */
static int8 compareRecords(LookupRecord const * record, LookupRecord const * recordOrKey)
{
	int8 atomOrder = CompareDatums(record->atom, recordOrKey->atom);
	if(atomOrder == 0) {
		if(recordOrKey->predicateForm) {
			int8 formOrder = CompareDatums(record->predicateForm, recordOrKey->predicateForm);
			if(formOrder == 0) {
				if(recordOrKey->role)
					return CompareDatums(record->role, recordOrKey->role);
				else
					return 0;
			}
			else return formOrder;
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


bool AtomHasRole(Atom atom, Atom predicateForm, Atom role)
{
	ASSERT(atom.type == DT_ID)
	if(predicateForm.datum) {
		ASSERT(predicateForm.type == DT_ID)
		if(role.datum) {
			ASSERT(role.type == DT_NAME)
		}
	}
	LookupRecord record;
	record.atom = atom.datum;
	record.predicateForm = predicateForm.datum;
	record.role = role.datum;

	return BTreeContainsItem(lookup.btree, &record);
}


void AtomAddRole(Atom atom, Atom predicateForm, Atom role)
{
	ASSERT(atom.type == DT_ID)
	ASSERT(predicateForm.type == DT_ID)
	ASSERT(role.type == DT_NAME)
	LookupRecord record;
	record.atom = atom.datum;
	record.predicateForm = predicateForm.datum;
	record.role = role.datum;

	LookupRecord * existingRecord = BTreePeekItem(lookup.btree, &record);
	if(existingRecord)
		existingRecord->nFacts++;
	else {
		record.nFacts = 1;
		ASSERT(BTreeInsert(lookup.btree, &record) == BTREE_INSERTED)
	}
	lookup.nRolesTotal++;
}


void LookupAddPredicateRoles(Atom predicateForm, Atom * actors)
{
	// iterate over roles names in the predicate form
	// and add entries to lookup table
	MultisetIterator formIterator;
	MultisetIterate(predicateForm, &formIterator);
	index8 index = 0;
	while(MultisetIteratorHasNext(&formIterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&formIterator);
		for(index8 i = 0; i < em.multiple; i++) {
			if(actors[index].type == DT_ID)
				AtomAddRole(actors[index], predicateForm, em.element);
			index++;
		}
		MultisetIteratorNext(&formIterator);
	}
	MultisetIteratorEnd(&formIterator);
}


void AtomRemoveRole(Atom atom, Atom predicateForm, Atom role)
{
	ASSERT(atom.type == DT_ID)
	ASSERT(predicateForm.type == DT_ID)
	ASSERT(role.type == DT_NAME)
	LookupRecord record;
	record.atom = atom.datum;
	record.predicateForm = predicateForm.datum;
	record.role = role.datum;

	LookupRecord * existingRecord = BTreePeekItem(lookup.btree, &record);
	ASSERT(existingRecord)
	if(existingRecord->nFacts > 1)
		existingRecord->nFacts--;
	else {
		ASSERT(BTreeDelete(lookup.btree, &record) == BTREE_DELETED)
	}
	lookup.nRolesTotal --;
}


void LookupRemoveAllRoles(Atom atom)
{
	ASSERT(atom.type == DT_ID);

	LookupRecord record;
	record.atom = atom.datum;
	record.predicateForm = 0;
	record.role = 0;

	// TODO: can we delete the item via the B-tree iterator more efficiently?
	while(BTreeGetItem(lookup.btree, &record)) {
		lookup.nRolesTotal -= record.nFacts;
		BTreeDelete(lookup.btree, &record);
		record.predicateForm = 0;
		record.role = 0;
	}
}


void LookupRemovePredicateRoles(Atom predicateForm, Atom * actors)
{
	MultisetIterator formIterator;
	MultisetIterate(predicateForm, &formIterator);
	index8 index = 0;
	while(MultisetIteratorHasNext(&formIterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&formIterator);
		for(index8 i = 0; i < em.multiple; i++) {
			if(actors[index].type == DT_ID)
				AtomRemoveRole(actors[index], predicateForm, em.element);
			index++;
		}
		MultisetIteratorNext(&formIterator);
	}
	MultisetIteratorEnd(&formIterator);
}


void LookupRemoveAllPredicateRoles(Atom predicateForm)
{
	// NOTE: this requires scanning the entire lookup table,
	// since it is indexed by atom, not predicate form.

	// Find all distinct atoms with a lookup entry for the given form.
	ResizingArray datumArray;
	CreateResizingArray(&datumArray, sizeof(Datum), 10);

	BTreeIterator iterator;
	BTreeIterate(&iterator, lookup.btree, 0, 0);
	Datum previousAtom = 0;
	while(BTreeIteratorHasItem(&iterator)) {
		LookupRecord const * record = BTreeIteratorPeekItem(&iterator);
		if(record->predicateForm == predicateForm.datum) {
			// since lookup entries are ordered by atom,
			// we can skip any entry with the same atom as previous
			if(record->atom != previousAtom) {
				ResizingArrayAppend(&datumArray, &(record->atom));
				previousAtom = record->atom;
			}
		}
		BTreeIteratorNext(&iterator);
	}
	BTreeIteratorEnd(&iterator);

	// Free all lookup entries for discovered atoms
	Datum const * datums = ResizingArrayGetMemory(&datumArray);
	size32 nAtoms = ResizingArrayNElements(&datumArray);
	for(index32 i = 0; i < nAtoms; i++)
		LookupRemoveAllRoles((Atom) {DT_ID, 0, 0, 0, datums[i]});
		
	FreeResizingArray(&datumArray);
}


void LookupIterate(Atom atom, LookupIterator * iterator)
{
	ASSERT(atom.type == DT_ID)
	iterator->query.atom = atom.datum;
	iterator->query.predicateForm = 0;
	iterator->query.role = 0;

	BTreeIterate(&(iterator->treeIterator), lookup.btree, &(iterator->query), 0);
}


/**
 * Test if the iterator has a next element.
 * If this function returns true, the tuple can be accessed by 
 * RelationBTreeIteratorGetTuple(). 
 */
bool LookupIteratorHasRecord(LookupIterator const * iterator)
{
	if(BTreeIteratorHasItem(&(iterator->treeIterator))) {
		LookupRecord const * record = BTreeIteratorPeekItem(&(iterator->treeIterator));
		if(compareRecords(record, &(iterator->query)) == 0)
			return true;
	}
	return false;
}


/**
 * Advance the iterator to the next record matching the query, if any
 */
void LookupIteratorNext(LookupIterator * iterator)
{
	BTreeIteratorNext(&(iterator->treeIterator));
}


Atom LookupIteratorGetForm(LookupIterator const * iterator)
{
	LookupRecord const * record = BTreeIteratorPeekItem(&(iterator->treeIterator));
	return (Atom) {DT_ID, 0, 0, 0, record->predicateForm};
}


Atom LookupIteratorGetRole(LookupIterator const * iterator)
{
	LookupRecord const * record = BTreeIteratorPeekItem(&(iterator->treeIterator));
	return (Atom) {DT_NAME, 0, 0, 0, record->role};
}


void FreeLookupIterator(LookupIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->treeIterator));
	SetMemory(iterator, sizeof(LookupIterator), 0);
}


void LookupDump(void)
{
	PrintF("Lookup table %u records:\n", BTreeNItems(lookup.btree));
	BTreeIterator iterator;
	BTreeIterate(&iterator, lookup.btree, 0, 0);
	while(BTreeIteratorHasItem(&iterator)) {
		LookupRecord const * record = BTreeIteratorPeekItem(&iterator);
		PrintAtom((Atom) {DT_ID, 0, 0, 0, record->atom});
		PrintChar(' ');
		PrintPredicateForm((Atom) {DT_ID, 0, 0, 0, record->predicateForm});
		PrintChar(' ');
		PrintName((Atom) {DT_NAME, 0, 0, 0, record->role});
		PrintF(" %u\n", record->nFacts);
		BTreeIteratorNext(&iterator);
	}
	BTreeIteratorEnd(&iterator);
}
