
#include "kernel/ifact.h"
#include "kernel/Relation.h"
#include "kernel/TupleStore.h"
#include "lang/ConjunctionForm.h"
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
	Relation relation;
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
	return CompareRelations(record->relation, recordOrKey->relation);
}


static RelationRecord * findRelationRecord(Relation relation)
{
	RelationRecord key = {.relation = relation};
	return BTreePeekItem(relationRegistry, &key);
}


void CreateRelationBootstrap(Relation relation, Atom predicateForm)
{
	// Create a new record
	RelationRecord record = {
		.relation = relation,
		.predicateForm = predicateForm,
		.ownsForm = true,
		.referenceCount = 1
	};
	IFactAcquire(relation.form);
	// Store a copy of the record in the B-tree
	ASSERT(BTreeInsert(relationRegistry, &record) == BTREE_INSERTED)
}


void AcquireRelation(Relation relation)
{
	RelationRecord * existingRecord = findRelationRecord(relation);
	if(existingRecord) {
		existingRecord->referenceCount++;
	}
	else {
		// CLAUDE: A conjunction form has no predicate form of its own
		Atom predicateForm = IsConjunctionForm(relation.form) ?
			(Atom) {0} : TermFormGetPredicateForm(relation.form);
		CreateRelationBootstrap(relation, predicateForm);
	}
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
			IFactRelease(record->relation.form);
		}
		RelationRecord key = {.relation = record->relation};
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

byte RelationAddTuple(Relation relation, Atom const tuple[], uint8 idPosition)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	ASSERT(record->tupleStore)
	ASSERT(TupleStoreIsWritable(record->tupleStore))
	return TupleStoreAddTuple(record->tupleStore, tuple, idPosition);
}


size32 RelationNRows(Relation relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	ASSERT(record->tupleStore)
	ASSERT(TupleStoreIsFinite(record->tupleStore))
	return TupleStoreNTuples(record->tupleStore);
}


byte RelationRemoveTuple(Relation relation, Atom const tuple[], uint8 idPosition)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	ASSERT(record->tupleStore)
	ASSERT(TupleStoreIsWritable(record->tupleStore))
	return TupleStoreRemoveTuple(record->tupleStore, tuple, idPosition);
}


void DropRelation(Relation relation)
{
	// Remove all services associated with the relation
	bool foundService;
	do {
		// NOTE: we should have a GetFirstMatchingItem() in BTree instead of this
		ServiceIterator iterator;
		ServiceRegistryIterate(relation, &iterator);
		Service service;
		foundService = false;
		while(ServiceIteratorNext(&iterator)) {
			ServiceRecord const * record = ServiceIteratorPeekRecord(&iterator);
			service = record->service;
			foundService = true;
			break;
		}
		// Close the iterator, since RemoveService() alters the service registry B-tree
		ServiceIteratorEnd(&iterator);
		if(foundService) {
			RemoveService(service);
		}
	} while(foundService);

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
	ServiceRegistryIterate(record->relation, &iterator);
	while(ServiceIteratorNext(&iterator)) {
		ServiceRecord const * record = ServiceIteratorPeekRecord(&iterator);
		if(record->op->type != OPERATOR_MACHINE) {
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
			ResizingArrayAppend(&relationsArray, &(record->relation));
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
	ASSERT(record->predicateForm.hash)
	return record->predicateForm;
}


bool RelationIsConjunction(Relation relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	return !record->predicateForm.hash;
}


bool RelationExists(Relation relation)
{
	return (findRelationRecord(relation) != 0);
}


int8 CompareRelations(Relation relation, Relation relationOrKey)
{
	// First compare forms
	if(relation.form.hash < relationOrKey.form.hash)
		return -1;
	else if(relation.form.hash > relationOrKey.form.hash)
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
	return SameAtoms(relation1.form, relation2.form) &&
		SameTypeSignatures(relation1.typeSignature, relation2.typeSignature);
}


bool IsNullRelation(Relation relation)
{
	return relation.form.hash == 0;
}


void RelationReleaseTermForm(Relation relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	ASSERT(record->ownsForm)
	// Clear the flag first, as IFactRelease() may retract tuples from this relation
	record->ownsForm = false;
	IFactRelease(record->relation.form);
}


data64 RelationHash(Relation relation, data64 initialHash)
{
	data64 hash = initialHash;
	// hash the form and types
	hash = DJB2DoubleHashAdd(&relation.form.hash, sizeof(data64), initialHash);
	hash = DJB2DoubleHashAdd(&relation.typeSignature.atomTypes, RELATION_MAX_ARITY, hash);
	return hash;
}


Relation RelationFromFact(FormulaView term)
{
	ASSERT(IsTermForm(term.form))
	TypeSignature typeSignature = CreateTypeSignature(
		TypedTuplePeekAtomTypes(term.actors), term.actors->nAtoms);
	
	// A generator atom corresponds to an AT_ID type
	for(index8 i = 0; i < term.actors->nAtoms; i++) {
		if(typeSignature.atomTypes[i] == AT_GENERATOR)
			typeSignature.atomTypes[i] = AT_ID;
	}

	return (Relation) {.form = term.form, .typeSignature = typeSignature};
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
	RelationRecord key = { .relation = { .form = iterator->form }};
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


bool IsRelationForm(Atom form)
{
	return IsTermForm(form) || IsConjunctionForm(form);
}
