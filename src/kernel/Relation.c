
#include "kernel/ifact.h"
#include "kernel/Relation.h"
#include "lang/TermForm.h"
#include "memory/allocator.h"
#include "util/hashing.h"


TypeSignature CreateTypeSignature(byte const atomTypes[], size8 nColumns)
{
	ASSERT(nColumns <= RELATION_MAX_ARITY)
	TypeSignature typeSignature = {.atomTypes = {0}};
	CopyMemory(atomTypes, typeSignature.atomTypes, nColumns);
	return typeSignature;
}


bool SameTypeSignatures(TypeSignature signature1, TypeSignature signature2)
{
	return CompareMemory(signature1.atomTypes, signature2.atomTypes, RELATION_MAX_ARITY) == 0;
}


typedef struct s_RelationRecord {
	Relation relation;
	// The predicate form of relation.termForm. Kept here because during bootstrap,
	// TermFormGetPredicateForm() is unreachable, and therefore LookupAddPredicateRoles()
	// would fail when creating the predicate form FORM_TERM_FORM in setupCoreService().
	Atom predicateForm;

	// Whether this relation holds a reference to its forms; see RelationReleaseForm()
	bool ownsForm;

	size32 referenceCount;
} RelationRecord;


/**
 * B-tree for lookup of relations by form, stores RelationRecord items.
 */
static BTree * relationRegistry;


static int8 btreeCompareRelationRecords(void const * item, void const * itemOrKey, size32 itemSize)
{
	RelationRecord const * record =  item;
	RelationRecord const * recordOrKey = itemOrKey;
	return CompareRelations(record->relation, recordOrKey->relation);
}


static RelationRecord * findRelationRecord(Relation relation)
{
	RelationRecord key = {.relation = relation};
	return BTreePeekItem(relationRegistry, &key);
}


Relation CreateRelationBootstrap(Atom termForm, Atom predicateForm, TypeSignature typeSignature)
{
	Relation relation = {.termForm = termForm, .typeSignature = typeSignature};
	// Check for an existing record	
	RelationRecord *existingRecord = findRelationRecord(relation);
	if(existingRecord) {
		existingRecord->referenceCount++;
		return relation;
	}
	// Else create a new record
	RelationRecord record = {
		.relation = relation,
		.predicateForm = predicateForm,
		.ownsForm = true,
		.referenceCount = 1
	};
	IFactAcquire(termForm);
	// Store a copy of the record in the B-tree
	ASSERT(BTreeInsert(relationRegistry, &record) == BTREE_INSERTED)
	return record.relation;
}

Relation CreateRelation(Atom termForm, TypeSignature typeSignature)
{
	return CreateRelationBootstrap(termForm, TermFormGetPredicateForm(termForm), typeSignature);
}


Atom RelationGetPredicateForm(Relation relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	return record->predicateForm;
}

bool RelationExists(Relation relation)
{
	return (findRelationRecord(relation) != 0);
}


int8 CompareRelations(Relation relation, Relation relationOrKey)
{
	// First compare forms
	if(relation.termForm.hash < relationOrKey.termForm.hash)
		return -1;
	else if(relation.termForm.hash > relationOrKey.termForm.hash)
		return 1;
	else {
		// then compare atom types
		if(!relationOrKey.typeSignature.atomTypes[0])
			return 0;
		return CompareMemory(
			relation.typeSignature.atomTypes, relationOrKey.typeSignature.atomTypes, RELATION_MAX_ARITY);
	}
}

bool SameRelations(Relation relation1, Relation relation2)
{
	return SameAtoms(relation1.termForm, relation2.termForm) &&
		SameTypeSignatures(relation1.typeSignature, relation2.typeSignature);
}


bool IsNullRelation(Relation relation)
{
	return relation.termForm.hash == 0;
}


void AcquireRelation(Relation relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	record->referenceCount++;
}


void ReleaseRelation(Relation relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	record->referenceCount--;
	if(record->referenceCount > 0)
		return;
	// Else remove the relation
	if(record->ownsForm) {
		// for all relatons except a few "core" relations
		IFactRelease(relation.termForm);
	}
	RelationRecord key = {.relation = relation};
	BTreeDelete(relationRegistry, &key, 0);
}


void RelationReleaseTermForm(Relation relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	ASSERT(record->ownsForm)
	// Clear the flag first, as IFactRelease() may retract tuples from this relation
	record->ownsForm = false;
	IFactRelease(record->relation.termForm);
}


data64 RelationHash(Relation relation, data64 initialHash)
{
	data64 hash = initialHash;
	// hash the form and types
	hash = DJB2DoubleHashAdd(&relation.termForm.hash, sizeof(data64), initialHash);
	hash = DJB2DoubleHashAdd(&relation.typeSignature.atomTypes, RELATION_MAX_ARITY, hash);
	return hash;
}


void SetupRelationRegistry(void)
{
	// The lookup B-tree stores pointers to RelationRecords items.
	relationRegistry = BTreeCreate(
		sizeof(RelationRecord),
		btreeCompareRelationRecords,
		0 // freeItem
	);
}


void FreeRelationRegistry(void)
{
	BTreeFree(relationRegistry);
}


size32 RelationRegistryNRelations(void)
{
	return BTreeNItems(relationRegistry);
}


void RelationRegistryIterate(Atom form, RelationIterator * iterator)
{
	iterator->form = form;
	BTreeIterate(&(iterator->btreeIterator), relationRegistry);
}


bool RelationIteratorNext(RelationIterator * iterator)
{
	// A key without type signature matches every relation for the term form
	RelationRecord key = { .relation = { .termForm = iterator->form }};
	bool foundItem;
	if(BTreeIteratorBeforeFirst(&(iterator->btreeIterator)))
		foundItem = BTreeIteratorSeek(&(iterator->btreeIterator), &key);
	else
		foundItem = BTreeIteratorNext(&(iterator->btreeIterator));
	if(foundItem) {
		RelationRecord * record = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(CompareRelations(record->relation, key.relation) == 0)
			return true;
	}
	return false;
}


Relation RelationIteratorGet(RelationIterator const * iterator)
{
	RelationRecord * record = BTreeIteratorPeekItem(&(iterator->btreeIterator));
	return record->relation;
}


void RelationIteratorEnd(RelationIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}
