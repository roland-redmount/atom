
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


size8 TypeSignatureNAtomTypes(TypeSignature typeSignature)
{
	size8 nColumns = 0;
	while(nColumns < RELATION_MAX_ARITY && typeSignature.atomTypes[nColumns])
		nColumns++;
	return nColumns;
}


typedef struct s_RelationRecord {
	RelationSignature signature;
	// The predicate form of relation.termForm. Kept here because during bootstrap,
	// TermFormGetPredicateForm() is unreachable, and therefore LookupAddPredicateRoles()
	// would fail when creating the predicate form FORM_TERM_FORM in setupCoreService().
	Atom predicateForm;

	/*
	 * The order of index columns. The stored tuples will be ordered lexicographically by
	 * indexColumns[0], ..., indexColumns[nColumns-1]. Hence, lookup should be fast when
	 * leading columns are specified in this order, while out-of-order
	 * columns may lead to table scanning.
	 * For example, a relation with canonical order (element list position) and
	 * indexColumns = {1, 2, 0} will be ordered first by list, then by position, then by element;
	 * queries (@list _ _) and (@list @position _) should be fast, but (_ _ @element) may be slow.
	 */
	index8 indexColumns[RELATION_MAX_ARITY];

	// The arity of the relation (NOTE: now also in impl.nColumns )
	size8 nColumns;

	// Implementation-dependent data for this relation, allocated by the StorageProvider.
	RelationImpl * impl; 

	// Whether this relation holds a reference to its forms; see RelationReleaseForm()
	bool ownsForm;

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


static RelationRecord * findRelationRecord(RelationSignature signature)
{
	RelationRecord key = {.signature = signature};
	return BTreePeekItem(relationRegistry, &key);
}


static Service createOperatorAndService(RelationRecord const * record, RelationReader * reader)
{
	Operator * op = CreateMachineOperator(record->nColumns, record->indexColumns, reader);

	// We must permute the reader IOSignature to match the Service order
	IOSignature serviceIOSignature = {.parameterIO = {0}};
	for(index8 j = 0; j < record->nColumns; j++) {
		serviceIOSignature.parameterIO[record->indexColumns[j]] = reader->spec.ioSignature.parameterIO[j];
	}
	return CreateService(record->signature, serviceIOSignature, op);
}


RelationSignature CreateRelationBootstrap(
	Atom termForm, Atom predicateForm, TypeSignature typeSignature,
	StorageProvider const * provider, index8 const indexColumns[])
{
	RelationSignature signature = {.termForm = termForm, .typeSignature = typeSignature};
	// The relation must not already exist
	ASSERT(!findRelationRecord(signature))
	// Create a new record
	RelationRecord record = {
		.signature = signature,
		.predicateForm = predicateForm,
		.nColumns = TypeSignatureNAtomTypes(typeSignature),
		.ownsForm = true
	};
	IFactAcquire(termForm);

	// setup index column array
	if(indexColumns)
		CopyMemory(indexColumns, record.indexColumns, record.nColumns);
	else {
		// use the identity order
		for(index8 i = 0; i < record.nColumns; i++)
			record.indexColumns[i] = i;
	}
	// Call the storage provider to setup the relation implementation and its readers
	record.impl = CreateRelationImpl(provider, record.nColumns);
	// Store a copy of the record in the B-tree
	ASSERT(BTreeInsert(relationRegistry, &record) == BTREE_INSERTED)

	// Create services for readers
	RelationReader * reader = record.impl->firstReader;
	for(index32 i = 0 ; i < record.impl->nReaders; i++) {
		ASSERT(reader)
		createOperatorAndService(&record, reader);
		reader = reader->next;
	}
	ASSERT(!reader)		// ensure nReaders == length of linked list

	return record.signature;
}

RelationSignature CreateRelation(
	Atom termForm, TypeSignature typeSignature, StorageProvider const * provider, index8 const indexColumns[])
{
	return CreateRelationBootstrap(
		termForm, TermFormGetPredicateForm(termForm), typeSignature, provider, indexColumns);
}


Service RelationAddPrimitiveService(RelationSignature relation, RelationReaderSpec const * readerSpec)
{
	RelationRecord * record = findRelationRecord(relation);

	// add reader to the relation implementation
	RelationReader * reader = RelationImplAddReader(record->impl, readerSpec);

	return createOperatorAndService(record, reader);
}


byte RelationAddTuple(RelationSignature signature, Atom const tuple[], uint8 idPosition)
{
	RelationRecord * record = findRelationRecord(signature);
	ASSERT(RelationImplIsWritable(record->impl))
	// Permute tuple to the provider's order
	Atom providerTuple[record->nColumns];
	for(index8 i = 0; i < record->nColumns; i++)
		providerTuple[i] = tuple[record->indexColumns[i]];
	index8 providerIdPositon = idPosition ? record->indexColumns[idPosition - 1] + 1 : 0;
	// Call the provider to store the tuple
	byte result = RelationImplAddTuple(record->impl, providerTuple, providerIdPositon);
	// Acquire atoms
	if(result == TUPLE_ADDED) {
		for(index8 i = 0; i < record->nColumns; i++) {
			if(i + 1 != idPosition)
				AcquireAtom(tuple[i], record->signature.typeSignature.atomTypes[i]);
		}
	}
	return result;
}


size32 RelationNColumns(RelationSignature signature)
{
	RelationRecord * record = findRelationRecord(signature);
	return record->nColumns;
}


size32 RelationNRows(RelationSignature signature)
{
	RelationRecord * record = findRelationRecord(signature);
	ASSERT(RelationImplIsWritable(record->impl))
	return RelationImplNRows(record->impl);
}


byte RelationRemoveTuple(RelationSignature signature, Atom const tuple[], uint8 idPosition)
{
	RelationRecord * record = findRelationRecord(signature);
	ASSERT(RelationImplIsWritable(record->impl))
	// Permute tuple to the provider's order
	Atom providerTuple[record->nColumns];
	for(index8 i = 0; i < record->nColumns; i++)
		providerTuple[i] = tuple[record->indexColumns[i]];
	index8 providerIdPositon = idPosition ? record->indexColumns[idPosition - 1] + 1 : 0;
	// Call the provider to remove th tuple
	byte result = RelationImplRemoveTuple(record->impl, providerTuple, providerIdPositon);

	// Release atoms
	if(result == TUPLE_REMOVED) {
		for(index32 i = 0; i < record->nColumns; i++) {
			if((i + 1) != idPosition)
				ReleaseTypedAtom(CreateTypedAtom(record->signature.typeSignature.atomTypes[i], tuple[i]));
		}
	}
	return result;
}

/**
 * Check whether the given operator refers to the table.
 */
static bool isRelationOperator(RelationRecord * record, Operator const * op)
{
	return op->impl.machine.reader->impl == record->impl;
}


void DropRelation(RelationSignature signature)
{
	RelationRecord * record = findRelationRecord(signature);
	ASSERT(record)
	// If the relation table has storage, it must be empty
	ASSERT(!RelationImplIsWritable(record->impl) || (RelationImplNRows(record->impl) == 0))


	if(record->ownsForm) {
		// for all relations except a few "core" relations
		IFactRelease(signature.termForm);
	}

	// Remove all services associated with the relation
	Service const * service;
	do {
		ServiceIterator iterator;
		ServiceRegistryIterate(record->signature, &iterator);
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

	FreeRelationImpl(record->impl);

	RelationRecord key = {.signature = signature};
	BTreeDelete(relationRegistry, &key, 0);
}


Atom RelationGetPredicateForm(RelationSignature relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	return record->predicateForm;
}

bool RelationExists(RelationSignature relation)
{
	return (findRelationRecord(relation) != 0);
}


int8 CompareRelations(RelationSignature relation, RelationSignature relationOrKey)
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

bool SameRelations(RelationSignature relation1, RelationSignature relation2)
{
	return SameAtoms(relation1.termForm, relation2.termForm) &&
		SameTypeSignatures(relation1.typeSignature, relation2.typeSignature);
}


bool IsNullRelation(RelationSignature relation)
{
	return relation.termForm.hash == 0;
}


void RelationReleaseTermForm(RelationSignature relation)
{
	RelationRecord * record = findRelationRecord(relation);
	ASSERT(record)
	ASSERT(record->ownsForm)
	// Clear the flag first, as IFactRelease() may retract tuples from this relation
	record->ownsForm = false;
	IFactRelease(record->signature.termForm);
}


data64 RelationHash(RelationSignature relation, data64 initialHash)
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


RelationSignature RelationIteratorGet(RelationIterator const * iterator)
{
	RelationRecord * record = BTreeIteratorPeekItem(&(iterator->btreeIterator));
	return record->signature;
}


void RelationIteratorEnd(RelationIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}
