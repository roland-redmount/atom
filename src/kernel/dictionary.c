#include "btree/btree.h"
#include "kernel/dictionary.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/typedtuple.h"
#include "lang/formula.h"
#include "lang/ClauseForm.h"
#include "memory/allocator.h"
#include "parser/ClauseBuilder.h"
#include "util/ResizingArray.h"


struct {
	// We keep a single B-tree for all entries
	BTree * btree;
} dictionary;


static int8 compareEntries(DictionaryEntry const * entry, DictionaryEntry const * entryOrKey)
{
	if(entry->clauseForm.hash < entryOrKey->clauseForm.hash)
		return -1;
	else if(entry->clauseForm.hash > entryOrKey->clauseForm.hash)
		return 1;
	else {
		if(!entryOrKey->tuple) {
			// no tuple provided
			return 0;
		}
		return TypedTupleCompare(entry->tuple, entryOrKey->tuple);
	}
}


static int8 btreeCompareItems(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareEntries(item, itemOrKey);
}


static void btreeFreeItem(void const * item, size32 itemSize)
{
	DictionaryEntry const * entry = item;
	IFactRelease(entry->clauseForm);
	FreeTypedTuple(entry->tuple);
}


void SetupDictionary(void)
{
	dictionary.btree = BTreeCreate(sizeof(DictionaryEntry), &btreeCompareItems, &btreeFreeItem);
}


void TeardownDictionary(void)
{
	BTreeFree(dictionary.btree);
}


static void setupEntry(DictionaryEntry * entry, Atom clauseForm, TypedTuple const * actors)
{
	entry->clauseForm = clauseForm;
	IFactAcquire(clauseForm);
	TypedTuple * tuple = CreateTypedTuple(actors->nAtoms);
	TypedTupleCopy(actors, tuple);
	entry->tuple = tuple;
}


/**
 * Invalidate any compiled services that given clause form could have contributed to,
 * which may now be stale. The compiler resolves a query against the clauses whose form contains
 * the query term form, so it is sufficient to invalidate services associated with any of the
 * term forms in the given clause. See compileQueryClauses().
 */
static void invalidateClauseServices(Atom clauseForm)
{
	if(NumberOfCompiledServices() == 0)
		return;

	// Collect the term forms before invalidating any service: the multiset iterator
	// evaluates a service of its own, and invalidation removes services
	ResizingArray termForms;
	CreateResizingArray(&termForms, sizeof(Atom), 8);
	MultisetIterator iterator;
	MultisetIterate(clauseForm, AT_ID, &iterator);
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple element = MultisetIteratorGetElement(&iterator);
		ResizingArrayAppend(&termForms, &(element.element));
	}
	MultisetIteratorEnd(&iterator);

	// Invalidate all term forms
	for(index32 i = 0; i < ResizingArrayNElements(&termForms); i++)
		InvalidateServicesByTermForm(*(Atom *) ResizingArrayGetElement(&termForms, i));
	FreeResizingArray(&termForms);
}


/**
 * Find the entry of the given clause, copying it to *entry if one is given, and return
 * whether the dictionary holds it. The key is the clause's own form and actors, which
 * compareEntries() only reads, so no entry has to be built to look one up.
 */
static bool findEntry(Atom clause, DictionaryEntry * entry)
{
	ASSERT(FormulaIsClause(clause))
	FormulaView clauseView = FormulaGetView(clause);
	DictionaryEntry key = {
		.clauseForm = clauseView.form,
		.tuple = clauseView.actors
	};
	if(entry)
		return BTreeGetItem(dictionary.btree, &key, entry);
	return BTreeContainsItem(dictionary.btree, &key);
}


bool DictionaryContainsClause(Atom clause)
{
	return findEntry(clause, 0);
}


DictionaryEntry DictionaryAddClause(Atom clause)
{
	// A clause the dictionary already holds is left as it is. Building an entry for it
	// would acquire a reference per actor that inserting it would then have to give back,
	// and would leave the entry it replaced with no owner.
	DictionaryEntry entry;
	if(findEntry(clause, &entry))
		return entry;

	FormulaView clauseView = FormulaGetView(clause);
	setupEntry(&entry, clauseView.form, clauseView.actors);
	ASSERT(BTreeInsert(dictionary.btree, &entry) == BTREE_INSERTED)
	invalidateClauseServices(entry.clauseForm);
	return entry;
}


DictionaryEntry DictionaryAddClauseFromCString(const char * clauseString)
{
	Atom rule = CStringToClause(clauseString);
	DictionaryEntry entry = DictionaryAddClause(rule);
	ReleaseFormula(rule);	
	return entry;
}


void DictionaryRemoveClause(DictionaryEntry * entry)
{
	// Invalidate before the entry goes: the clause form is released with it, and the
	// compiled services are stale either way
	invalidateClauseServices(entry->clauseForm);
	ASSERT(BTreeDelete(dictionary.btree, entry, 0) == BTREE_DELETED)
}


void DictionaryRemoveAll(void)
{
	BTreeClear(dictionary.btree);
	RemoveAllCompiledServices();
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


TypedTuple const * DictionaryIteratorPeekActors(DictionaryIterator * iterator)
{
	DictionaryEntry * entry = BTreeIteratorPeekItem(&(iterator->btreeIterator));
	return entry->tuple;
}


void DictionaryIteratorEnd(DictionaryIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
	SetMemory(iterator, sizeof(DictionaryIterator), 0);
}
