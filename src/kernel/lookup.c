
#include "btree/btree.h"
#include "kernel/lookup.h"
#include "kernel/multiset.h"
#include "lang/Atom.h"
#include "lang/ConjunctionForm.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "util/ResizingArray.h"


// TODO: move this to persistent memory
struct s_Lookup {
	BTree * btree;
	size32 nRolesTotal;
} lookup;


/**
 * Comparison function for lookup records, used for both queries and item ordering.
 * Order by atom, then by relation, then by termForm, then by role.
 */
static int8 compareRecords(LookupRecord const * record, LookupRecord const * recordOrKey)
{
	int8 atomOrder = CompareAtoms(record->atom, recordOrKey->atom);
	if(atomOrder == 0) {
		if(!IsNullRelation(recordOrKey->relation)) {
			int8 relationOrder = CompareRelations(record->relation, recordOrKey->relation);
			if(relationOrder == 0) {
				if(recordOrKey->termForm.hash) {
					int8 termFormOrder = CompareAtoms(record->termForm, recordOrKey->termForm);
					if(termFormOrder == 0) {
						if(recordOrKey->role.hash)
							return CompareAtoms(record->role, recordOrKey->role);
						else
							return 0;
					}
					else
						return termFormOrder;
				}
				else
					return 0;
			}
			else
				return relationOrder;
		}
		else
			return 0;
	}
	else
		return atomOrder;
}


static int8 btreeCompareRecords(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareRecords(item, itemOrKey);
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


bool LookupHasEntry(Atom atom, Relation relation, Atom termForm, Atom role)
{
	ASSERT(!IsNullRelation(relation))
	ASSERT(termForm.hash)
	ASSERT(role.hash)
	LookupRecord record = {
		.atom = atom,
		.relation = relation,
		.termForm = termForm,
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


void LookupAddRole(Atom atom, Relation relation, Atom termForm, Atom role)
{
	LookupRecord record = {
		.atom = atom,
		.relation = relation,
		.termForm = termForm,
		.role = role
	};
	// TODO: We should verify that the role actally exists in the given relation
	addRecord(&record);
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


void LookupRemoveRole(Atom atom, Relation relation, Atom termForm, Atom role)
{
	LookupRecord record = {
		.atom = atom,
		.relation = relation,
		.termForm = termForm,
		.role = role
	};
	removeRecord(&record);
}


void LookupRemoveAllRoles(Atom atom)
{
	LookupRecord key = { .atom = atom };
	LookupRecord record;
	// TODO: can we delete the item via the B-tree iterator more efficiently?
	while(BTreeGetItem(lookup.btree, &key, &record)) {
		lookup.nRolesTotal -= record.nFacts;
		BTreeDelete(lookup.btree, &record, 0);
		record.relation = (Relation) {0};
		record.role = (Atom) {0};
	}
}


/**
 * CLAUDE: Call updateRecord() with a lookup record for each AT_ID actor of one term
 * in a fact. The term's actors start at actorsOffset in the actors array of the fact.
 */
static void updateTermRoles(
	Relation relation, Atom termForm, Atom predicateForm, index8 actorsOffset, Atom const actors[],
	void (*updateRecord)(LookupRecord *))
{
	LookupRecord record = {
		.relation = relation,
		.termForm = termForm
	};

	MultisetIterator formIterator;
	MultisetIterate(predicateForm, AT_NAME, &formIterator);
	index8 index = actorsOffset;
	while(MultisetIteratorNext(&formIterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&formIterator);
		for(index8 i = 0; i < em.multiple; i++, index++) {
			if(relation.typeSignature.atomTypes[index] != AT_ID)
				continue;
			record.atom = actors[index];
			record.role = em.element;
			updateRecord(&record);
		}
	}
	MultisetIteratorEnd(&formIterator);
}


/**
 * CLAUDE: Call updateRecord() with a lookup record for each AT_ID actor in a fact.
 * The relation's form is tested with RelationIsConjunction(), since IsTermForm()
 * does not work during bootstrap.
 */
static void updateFactRoles(
	Relation relation, Atom const actors[], void (*updateRecord)(LookupRecord *))
{
	if(!RelationIsConjunction(relation)) {
		Atom predicateForm = RelationGetPredicateForm(relation);
		updateTermRoles(relation, relation.form, predicateForm, 0, actors, updateRecord);
	}
	else {
		MultisetIterator formIterator;
		MultisetIterate(relation.form, AT_ID, &formIterator);
		index8 actorsOffset = 0;
		while(MultisetIteratorNext(&formIterator)) {
			ElementMultiple em = MultisetIteratorGetElement(&formIterator);
			Atom termForm = em.element;
			size8 termArity = TermFormArity(termForm);
			Atom predicateForm = TermFormGetPredicateForm(termForm);
			for(index8 i = 0; i < em.multiple; i++) {
				updateTermRoles(relation, termForm, predicateForm, actorsOffset, actors, updateRecord);
				actorsOffset += termArity;
			}
		}
		MultisetIteratorEnd(&formIterator);
	}
}


void LookupAddFactRoles(Relation relation, Atom const actors[])
{
	// iterate over roles names in the relation
	// and add corresponding actors to lookup table
	updateFactRoles(relation, actors, addRecord);
}


void LookupRemoveFactRoles(Relation relation, Atom const actors[])
{
	updateFactRoles(relation, actors, removeRecord);
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
		if(SameAtoms(record->predicateForm, predicateForm)) {
			// since lookup entries are ordered by atom,
			// we can skip any entry with the same atom as previous
			if(!SameAtoms(record->atom, previousAtom)) {
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
	iterator->query = (LookupRecord) { .atom = atom };
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


Relation LookupIteratorGetRelation(LookupIterator const * iterator)
{
	LookupRecord const * record = BTreeIteratorPeekItem(&(iterator->treeIterator));
	return record->relation;
}


Atom LookupIteratorGetTermForm(LookupIterator const * iterator)
{
	LookupRecord const * record = BTreeIteratorPeekItem(&(iterator->treeIterator));
	return record->termForm;
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


Relation LookupFindRelation(Atom atom, Atom termForm, Atom role)
{
	Relation relation = {0};
	LookupIterator iterator;
	LookupIterate(atom, &iterator);
	while(LookupIteratorNext(&iterator)) {
		Atom currentRole = LookupIteratorGetRole(&iterator);
		Relation currentRelation = LookupIteratorGetRelation(&iterator);

		if(SameAtoms(currentRole, role) && SameAtoms(currentRelation.form, termForm)) {
			ASSERT(IsNullRelation(relation))		// ensure we have only 1 matching relation
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
		PrintForm(record->relation.form);
		PrintChar(' ');
		PrintForm(record->termForm);
		PrintChar(' ');
		PrintName(record->role);
		PrintF(" %u\n", record->nFacts);
	}
	BTreeIteratorEnd(&iterator);
}
