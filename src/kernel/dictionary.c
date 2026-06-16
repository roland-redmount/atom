#include "btree/btree.h"
#include "kernel/dictionary.h"
#include "kernel/list.h"
#include "kernel/tuple.h"
#include "lang/Formula.h"
#include "lang/ClauseForm.h"
#include "memory/allocator.h"
#include "parser/ClauseBuilder.h"


struct {
	// We keep a single B-tree for all entries
	BTree * btree;
} dictionary;


static int8 compareEntries(DictionaryEntry const * entry, DictionaryEntry const * entryOrKey)
{
	if(entry->clauseForm < entryOrKey->clauseForm)
		return -1;
	else if(entry->clauseForm > entryOrKey->clauseForm)
		return 1;
	else {
		if(!entryOrKey->tuple) {
			// no tuple provided
			return 0;
		}
		return CompareTuples(entry->tuple, entryOrKey->tuple);
	}
}


static int8 btreeCompareItems(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareEntries(item, itemOrKey);
}


static void btreeFreeItem(void * item, size32 itemSize)
{
	DictionaryEntry * entry = item;
	IFactRelease(entry->clauseForm);
	for(index8 i = 0; i < entry->tuple->nAtoms; i++)
		ReleaseTypedAtom(TupleGetElement(entry->tuple, i));
	FreeTuple(entry->tuple);
}


void SetupDictionary(void)
{
	dictionary.btree = BTreeCreate(sizeof(DictionaryEntry), &btreeCompareItems, &btreeFreeItem);
}


void TeardownDictionary(void)
{
	BTreeFree(dictionary.btree);
}


static void setupEntry(DictionaryEntry * entry, Atom clauseForm, Tuple * actors)
{
	entry->clauseForm = clauseForm;
	IFactAcquire(clauseForm);
	entry->tuple = actors;
	for(index8 i = 0; i < actors->nAtoms; i++) 
		AcquireTypedAtom(TupleGetElement(actors, i));
}


DictionaryEntry DictionaryAddClause(Atom clause)
{
	ASSERT(IsFormula(clause))
	ASSERT(FormulaIsClause(clause))
	Atom clauseForm = FormulaGetForm(clause);
	size8 arity = ClauseArity(clauseForm);
	// Atom actorsList = FormulaGetActors(clause);
	Tuple * actors = CreateTuple(arity);
	CopyListToTuple(FormulaGetActors(clause), actors);

	DictionaryEntry entry;
	setupEntry(&entry, clauseForm, actors);
	ASSERT(BTreeInsert(dictionary.btree, &entry) == BTREE_INSERTED)
	return entry;
}


DictionaryEntry DictionaryAddClauseFromCString(const char * clauseString)
{
	Atom rule = CStringToClause(clauseString);
	DictionaryEntry entry = DictionaryAddClause(rule);
	IFactRelease(rule);	
	return entry;
}


void DictionaryRemoveClause(DictionaryEntry * entry)
{
	ASSERT(BTreeDelete(dictionary.btree, entry) == BTREE_DELETED)
}


void DictionaryRemoveAll(void)
{
	BTreeClear(dictionary.btree);
}


void DictionaryIterate(Atom clauseForm, DictionaryIterator * iterator)
{
	ASSERT(IsClauseForm(clauseForm))
	// size8 arity = ClauseArity(clauseForm);
	iterator->key =  (DictionaryEntry) {
		.clauseForm = clauseForm,
		.tuple = 0
	};
	BTreeIterate(&(iterator->btreeIterator), dictionary.btree);
}


bool DictionaryIteratorNext(DictionaryIterator * iterator)
{
	if(!iterator->btreeIterator.btree)
		return false;
	bool foundItem;
	if(BTreeIteratorBeforeFirst(&(iterator->btreeIterator)))
		foundItem = BTreeIteratorSeek(&(iterator->btreeIterator), &iterator->key);
	else
		foundItem = BTreeIteratorNext(&(iterator->btreeIterator));

	if(foundItem) {
		DictionaryEntry const * btreeEntry = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(compareEntries(btreeEntry, &iterator->key) == 0)
			return true;
	}
	return false;
}


Tuple const * DictionaryIteratorPeekActors(DictionaryIterator * iterator)
{
	DictionaryEntry * entry = BTreeIteratorPeekItem(&(iterator->btreeIterator));
	return entry->tuple;
}


void DictionaryIteratorEnd(DictionaryIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
	SetMemory(iterator, sizeof(DictionaryIterator), 0);
}
