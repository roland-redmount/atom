#include "btree/btree.h"
#include "kernel/dictionary.h"
#include "kernel/list.h"
#include "kernel/tuple.h"
#include "lang/Formula.h"
#include "lang/ClauseForm.h"


#define MAX_ARITY	20

struct {
	// We keep one B-tree for arity, plus a dummy for arity 0
	BTree * btrees[MAX_ARITY + 1];
} dictionary;


/**
 * A dictionary record consists of a form atom and a tuple
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


static int8 compareRecords(void const * item, void const * itemOrKey, size32 itemSize)
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


static void createRecord(Atom clause, byte * record, bool acquireAtoms)
{
	Atom clauseForm = FormulaGetForm(clause);
	Atom actorsList = FormulaGetActors(clause);
	size8 arity = ClauseArity(clauseForm);

	if(acquireAtoms)
		IFactAcquire(clauseForm);
	recordSetForm(record, clauseForm);
	// copy actors list to record and acquire atoms
	Tuple * tuple = recordPeekTuple(record);
	SetupTuple(tuple, arity);
	for(index8 i = 0; i < arity; i++) {
		TypedAtom element = ListGetElement(actorsList, i + 1);
		if(acquireAtoms)
			AcquireTypedAtom(element);
		TupleSetElement(tuple, i, element);
	}
}


void DictionaryAddClause(Atom clause)
{
	ASSERT(IsFormula(clause))
	ASSERT(FormulaIsClause(clause))

	size8 arity = FormulaArity(clause);
	byte record[recordSize(arity)];
	createRecord(clause, record, true);
	
	// create B-tree if it does not exist
	if(!dictionary.btrees[arity])
		dictionary.btrees[arity] = BTreeCreate(recordSize(arity), &compareRecords, &freeRecord);
	
	// add record
	ASSERT(BTreeInsert(dictionary.btrees[arity], &record) == BTREE_INSERTED)
}


void DictionaryRemoveClause(Atom clause)
{
	ASSERT(IsFormula(clause))
	ASSERT(FormulaIsClause(clause))

	size8 arity = FormulaArity(clause);
	byte record[recordSize(arity)];
	createRecord(clause, record, false);

	ASSERT(dictionary.btrees[arity])
	ASSERT(BTreeDelete(dictionary.btrees[arity], record) == BTREE_DELETED)
	// remove B-tree if empty
	if(BTreeNItems(dictionary.btrees[arity]) == 0) {
		BTreeFree(dictionary.btrees[arity]);
		dictionary.btrees[arity] = 0;
	}
}
