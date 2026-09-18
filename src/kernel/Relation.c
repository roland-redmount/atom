
#include "kernel/ifact.h"
#include "kernel/Relation.h"
#include "kernel/TupleStore.h"
#include "lang/TermForm.h"
#include "memory/allocator.h"
#include "util/hashing.h"
#include "util/ResizingArray.h"


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


size8 TypeSignatureNAtomTypes(TypeSignature typeSignature)
{
	size8 nColumns = 0;
	while(nColumns < RELATION_MAX_ARITY && typeSignature.atomTypes[nColumns])
		nColumns++;
	return nColumns;
}


typedef struct s_RelationRecord {
	Relation signature;
	// The predicate form of relation.termForm. Kept here because during bootstrap,
	// TermFormGetPredicateForm() is unreachable, and therefore LookupAddPredicateRoles()
	// would fail when creating the predicate form FORM_TERM_FORM in setupCoreService().
	Atom predicateForm;
	// Whether this relation holds a reference to its forms; see RelationReleaseForm()
	bool ownsForm;

	TupleStore * tupleStore;	// may be 0
	uint32 referenceCount;
} RelationRecord;


/**
 * B-tree for lookup of relations by form, stores RelationRecord items.
 */
static BTree * relationRegistry;


static int8 btreeCompareRelationRecords(void const * item, void const * itemOrKey, size32 itemSize)
{
	RelationRecord const * record =  item;
	RelationRecord const * recordOrKey = itemOrKey;
	return CompareRelations(record->signature, recordOrKey->signature);
}


static RelationRecord * findRelationRecord(Relation signature)
{
	RelationRecord key = {.signature = signature};
	return BTreePeekItem(relationRegistry, &key);
}


void CreateRelationBootstrap(Relation relation, Atom predicateForm)
{
	// Create a new record
	RelationRecord record = {
		.signature = relation,
		.predicateForm = predicateForm,
		.ownsForm = true,
		.referenceCount = 1
	};
	IFactAcquire(relation.termForm);
	// Store a copy of the record in the B-tree
	ASSERT(BTreeInsert(relationRegistry, &record) == BTREE_INSERTED)
}


void AcquireRelation(Relation relation)
{
	RelationRecord * existingRecord = findRelationRecord(relation);
	if(existingRecord) {
		existingRecord->referenceCount++;
	}
	else
		CreateRelationBootstrap(relation, TermFormGetPredicateForm(relation.termForm));
}


void ReleaseRelation(Relation relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	ASSERT(record->referenceCount > 0)
	record->referenceCount--;
	if(record->referenceCount == 0) {
		// At this point, no services refer to the relation,
		// and we must have no tuple store.
		ASSERT(!record->tupleStore)

		if(record->ownsForm) {
			// for all relations except a few "core" relations
			IFactRelease(record->signature.termForm);
		}
		RelationRecord key = {.signature = record->signature};
		BTreeDelete(relationRegistry, &key, 0);
	}
}

/**
 * Set the relation's tuple store. This should only be called from CreateTupleStore().
 */
void RelationAttachTupleStore(Relation relation, TupleStore * store)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	ASSERT(!record->tupleStore)
	record->tupleStore = store;	
}

/**
 * Unset the relation's tuple store. This should only be called from CreateTupleStore().
 */
void RelationDetachTupleStore(Relation relation, TupleStore * store)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	ASSERT(record->tupleStore)
	record->tupleStore = 0;	
}


/**
 * Return the TupleStore associated with this Relation, or 0 if none exists.
 */
TupleStore * RelationGetTupleStore(Relation relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	return record->tupleStore;
}

byte RelationAddTuple(Relation signature, Atom const tuple[], uint8 idPosition)
{
	RelationRecord * record = findRelationRecord(signature);
	ASSERT(TupleStoreIsWritable(record->tupleStore))
	return TupleStoreAddTuple(record->tupleStore, tuple, idPosition);
}


size32 RelationNRows(Relation signature)
{
	RelationRecord * record = findRelationRecord(signature);
	ASSERT(record)
	ASSERT(record->tupleStore)
	ASSERT(TupleStoreIsFinite(record->tupleStore))
	return TupleStoreNTuples(record->tupleStore);
}


byte RelationRemoveTuple(Relation signature, Atom const tuple[], uint8 idPosition)
{
	RelationRecord * record = findRelationRecord(signature);
	ASSERT(record)
	ASSERT(record->tupleStore)
	ASSERT(TupleStoreIsWritable(record->tupleStore))
	return TupleStoreRemoveTuple(record->tupleStore, tuple, idPosition);
}


void DropRelation(Relation relation)
{
	// Remove all services associated with the relation
	Service const * service;
	do {
		ServiceIterator iterator;
		ServiceRegistryIterate(relation, &iterator);
		service = 0;
		while(ServiceIteratorNext(&iterator)) {
			Service const * candidate = ServiceIteratorPeekService(&iterator);
			service = candidate;
			break;
		}
		// Close the iterator, since RemoveService() alters the service registry B-tree
		ServiceIteratorEnd(&iterator);
		if(service) {
			RemoveService(service->relation, service->op);
		}
	} while(service);

	// If the relation had no tuple store, it has already been removed at this point
	RelationRecord * record = findRelationRecord(relation);
	if(record) {
		// if not, it must have a tupleStore holding the last reference
		ASSERT(record->tupleStore)
		DropTupleStore(record->tupleStore);
	}
	// The relation should now have been removed
	ASSERT(!RelationExists(relation))
}


static bool relationIsEmpty(RelationRecord * record)
{
	if(!record->tupleStore || !TupleStoreIsWritable(record->tupleStore)) {
		// A relation without a tuple store, or with a read-only tuple store
		// is never considered empty
		return false;
	}
	if(TupleStoreNTuples(record->tupleStore) > 0)
		return false;
	// check for a non-primitive service
	bool hasNonPrimitiveService = false;
	ServiceIterator iterator;
	ServiceRegistryIterate(record->signature, &iterator);
	while(ServiceIteratorNext(&iterator)) {
		Service const * service = ServiceIteratorPeekService(&iterator);
		if(service->op->type != OPERATOR_MACHINE) {
			hasNonPrimitiveService = true;
			break;
		}
	}
	ServiceIteratorEnd(&iterator);
	return !hasNonPrimitiveService;
}

void DropEmptyRelations(void)
{
	// collect all empty relations
	ResizingArray relationsArray;
	CreateResizingArray(&relationsArray, sizeof(Relation), 10);
	BTreeIterator iterator;
	BTreeIterate(&iterator, relationRegistry);
	while(BTreeIteratorNext(&iterator)) {
		RelationRecord * record = BTreeIteratorPeekItem(&iterator);
		if(relationIsEmpty(record))
			ResizingArrayAppend(&relationsArray, &(record->signature));
	}
	BTreeIteratorEnd(&iterator);

	for(index32 i = 0; i < relationsArray.nElements; i++) {
		Relation * relation = ResizingArrayGetElement(&relationsArray, i);
		DropRelation(*relation);
	}
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


void RelationReleaseTermForm(Relation relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	ASSERT(record->ownsForm)
	// Clear the flag first, as IFactRelease() may retract tuples from this relation
	record->ownsForm = false;
	IFactRelease(record->signature.termForm);
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
	RelationRecord key = { .signature = { .termForm = iterator->form }};
	bool foundItem;
	if(BTreeIteratorBeforeFirst(&(iterator->btreeIterator)))
		foundItem = BTreeIteratorSeek(&(iterator->btreeIterator), &key);
	else
		foundItem = BTreeIteratorNext(&(iterator->btreeIterator));
	if(foundItem) {
		RelationRecord * record = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(CompareRelations(record->signature, key.signature) == 0)
			return true;
	}
	return false;
}


Relation RelationIteratorGet(RelationIterator const * iterator)
{
	RelationRecord * record = BTreeIteratorPeekItem(&(iterator->btreeIterator));
	return record->signature;
}


void RelationIteratorEnd(RelationIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}
