#include "btree/btree.h"
#include "kernel/dictionary.h"
#include "kernel/list.h"
#include "kernel/tuple.h"
#include "lang/Formula.h"
#include "lang/ClauseForm.h"
#include "memory/allocator.h"


#define MAX_ARITY	20

struct {
	// We keep one B-tree for arity, plus a dummy for arity 0
	BTree * btrees[MAX_ARITY + 1];
} dictionary;


/**
 * A dictionary record consists of a form atom and a Tuple
 */
static size32 recordSize(size8 arity)
{
	return sizeof(Atom) + TupleNBytes(arity);
}


static Atom recordGetForm(byte const * record)
{
	return *((Atom *) record);
}


static void recordSetForm(byte const * record, Atom form)
{
	*((Atom *) record) = form;
}


static Tuple * recordPeekTuple(byte * record)
{
	return (Tuple *) (record + sizeof(Atom));
}


static int8 compareRecords(byte const * item, byte const * itemOrKey)
{
	Atom recordForm = recordGetForm(item);
	Atom recordOrKeyForm = recordGetForm(itemOrKey);
	if(recordForm < recordOrKeyForm)
		return -1;
	else if(recordForm > recordOrKeyForm)
		return 1;
	else {
		Tuple const * recordTuple = recordPeekTuple((byte *) item);
		Tuple const * recordOrKeyTuple = recordPeekTuple((byte *) itemOrKey);
		if(!recordOrKeyTuple->nAtoms) {
			// no tuple provided
			return 0;
		}
		return CompareTuples(recordTuple, recordOrKeyTuple);
	}
}


static int8 btreeCompareRecords(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareRecords(item, itemOrKey);
}


static void freeRecord(void * item, size32 itemSize)
{
	IFactRelease(recordGetForm(item));
	Tuple const * recordTuple = recordPeekTuple((byte *) item);
	for(index8 i = 0; i < recordTuple->nAtoms; i++)
		ReleaseTypedAtom(TupleGetElement(recordTuple, i));
}


void SetupDictionary(void)
{
	// B-trees are allocated when needed, init to zero
	SetMemory(&dictionary, sizeof(dictionary), 0);
}


static void setupRecord(Atom clauseForm, Atom actorsList, byte * record, bool acquireAtoms)
{
	size8 arity = ClauseArity(clauseForm);

	if(acquireAtoms)
		IFactAcquire(clauseForm);
	recordSetForm(record, clauseForm);
	// copy actors list to record and acquire atoms
	Tuple * tuple = recordPeekTuple(record);
	if(actorsList) {
		SetupTuple(tuple, arity);
		for(index8 i = 0; i < arity; i++) {
			TypedAtom element = ListGetElement(actorsList, i + 1);
			if(acquireAtoms)
				AcquireTypedAtom(element);
			TupleSetElement(tuple, i, element);
		}
	}
	else {
		// this represents "no tuple"
		SetupTuple(tuple, 0);
	}

}


void DictionaryAddClause(Atom clause)
{
	ASSERT(IsFormula(clause))
	ASSERT(FormulaIsClause(clause))
	Atom clauseForm = FormulaGetForm(clause);
	Atom actorsList = FormulaGetActors(clause);

	size8 arity = ClauseArity(clauseForm);
	byte record[recordSize(arity)];
	setupRecord(clauseForm, actorsList, record, true);
	
	// create B-tree if it does not exist
	if(!dictionary.btrees[arity])
		dictionary.btrees[arity] = BTreeCreate(recordSize(arity), &btreeCompareRecords, &freeRecord);
	
	// add record
	ASSERT(BTreeInsert(dictionary.btrees[arity], &record) == BTREE_INSERTED)
}


void DictionaryRemoveClause(Atom clause)
{
	ASSERT(IsFormula(clause))
	ASSERT(FormulaIsClause(clause))
	Atom clauseForm = FormulaGetForm(clause);
	Atom actorsList = FormulaGetActors(clause);

	size8 arity = ClauseArity(clauseForm);
	byte record[recordSize(arity)];
	setupRecord(clauseForm, actorsList, record, false);

	ASSERT(dictionary.btrees[arity])
	ASSERT(BTreeDelete(dictionary.btrees[arity], record) == BTREE_DELETED)
	// remove B-tree if empty
	if(BTreeNItems(dictionary.btrees[arity]) == 0) {
		BTreeFree(dictionary.btrees[arity]);
		dictionary.btrees[arity] = 0;
	}
}


void DictionaryIterate(Atom clauseForm, DictionaryIterator * iterator)
{
	ASSERT(IsClauseForm(clauseForm))
	size8 arity = ClauseArity(clauseForm);
	iterator->keyRecord = Allocate(recordSize(arity));
	setupRecord(clauseForm, 0, iterator->keyRecord, false);
	if(!dictionary.btrees[arity]) {
		// no rules for clauses of this arity
		iterator->btreeIterator = (BTreeIterator) {0};
	}
	else {
		BTreeIterate(&(iterator->btreeIterator), dictionary.btrees[arity]);
		BTreeIteratorSeek(&(iterator->btreeIterator), iterator->keyRecord);
	}
}


bool DictionaryIteratorHasRecord(DictionaryIterator * iterator)
{
	if(!iterator->btreeIterator.btree)
		return false;
		
	if(BTreeIteratorHasItem(&(iterator->btreeIterator))) {
		byte const * btreeRecord = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(compareRecords(btreeRecord, iterator->keyRecord) == 0)
			return true;
	}
	return false;
}


Tuple const * DictionaryIteratorPeekActors(DictionaryIterator * iterator)
{
	byte const * record = BTreeIteratorPeekItem(&(iterator->btreeIterator));
	return recordPeekTuple((byte *) record);
}


void DictionaryIteratorNext(DictionaryIterator * iterator)
{
	BTreeIteratorNext(&(iterator->btreeIterator));
}


void DictionaryIteratorEnd(DictionaryIterator * iterator)
{
	Free(iterator->keyRecord);
	BTreeIteratorEnd(&(iterator->btreeIterator));
}
