
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/lookup.h"
#include "kernel/Parameter.h"
#include "kernel/RelationBTree.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/UInt.h"
#include "lang/TypedAtom.h"
#include "lang/Form.h"
#include "lang/Formula.h"
#include "lang/PredicateForm.h"
#include "memory/allocator.h"
#include "memory/pool.h"
#include "util/hashing.h"
#include "util/ResizingArray.h"


/**
 * The registry for both relations and their associated services.
 * We store RelationTable entries in a pool for stable allocation.
 */
struct {
	// B-tree for lookup of relations by form, stores RelationTable * pointers as items
	BTree * relationBTree;
	// B-tree storing ServiceRecord items
	BTree * services;
} registry;


static int8 compareRelations(RelationTable const * relation, RelationTable const * relationOrKey)
{
	// First compare forms
	if(relation->form.hash < relationOrKey->form.hash)
		return -1;
	else if(relation->form.hash > relationOrKey->form.hash)
		return 1;
	else {
		// then compare atom types
		return CompareMemory(relation->atomTypes, relationOrKey->atomTypes, relation->nColumns);
	}
}


static int8 btreeCompareRelations(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareRelations(*((RelationTable **) item), *((RelationTable **) itemOrKey));
}


static void btreeFreeRelation(void * item, size32 itemSize)
{
	RelationTable * relation = *((RelationTable **) item);
	FreeRelationTable(relation);
}


static void setupServiceRecord(
	ServiceRecord * record, RelationTable const * relation, byte const parameterIO[], Service * service)
{
	record->relation = relation;
	if(parameterIO) {
		record->parameterIO = Allocate(relation->nColumns);
		CopyMemory(parameterIO, record->parameterIO, relation->nColumns);
	}
	else
		record->parameterIO = 0;
	record->service = service;
}


// callback to free the parameterIO allocation upon B-tree deletion of a ServiceRecord
static void btreeFreeServiceRecord(void * item, size32 itemSize)
{
	ServiceRecord * record = item;
	if(record->parameterIO)
		Free(record->parameterIO);
}


static int8 compareServiceRecords(ServiceRecord const * record, ServiceRecord const * recordOrKey)
{
	// First compare relations
	if(record->relation < recordOrKey->relation)
		return -1;
	else if(record->relation > recordOrKey->relation)
		return 1;
	else {
		// then compare parameter IO lists
		if(!recordOrKey->parameterIO)
			return 0;
		ASSERT(record->parameterIO)
		return CompareMemory(record->parameterIO, recordOrKey->parameterIO, record->relation->nColumns);
	}
}


static int8 btreeCompareServiceRecords(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareServiceRecords((ServiceRecord *) item, (ServiceRecord *) itemOrKey);
}


void SetupRegistry(void)
{
	// The lookup B-tree stores pointers to RelationTable records
	registry.relationBTree = BTreeCreate(
		sizeof(RelationTable *),
		btreeCompareRelations,
		btreeFreeRelation
	);
	// mapping relations -> services
	registry.services = BTreeCreate(
		sizeof(ServiceRecord),
		btreeCompareServiceRecords,
		btreeFreeServiceRecord
	);
}


void FreeRegistry(void)
{
	BTreeFree(registry.services);
	BTreeFree(registry.relationBTree);
}


void RegistryAddRelationTable(RelationTable const * relation)
{
	ASSERT(BTreeInsert(registry.relationBTree, &relation) == BTREE_INSERTED)
}


void RegistryRemoveRelationTable(RelationTable const * relation)
{
	ASSERT(BTreeDelete(registry.relationBTree, &relation, 0))
}


void RelationAddService(RelationTable const * relation, byte const parameterIO[], Service * service)
{
	ServiceRecord record;
	setupServiceRecord(&record, relation, parameterIO, service);
	ASSERT(BTreeInsert(registry.services, &record) == BTREE_INSERTED)
	AcquireService(service);
}


void RelationRemoveService(RelationTable const * relation, Service * service)
{
	ServiceRecord key;
	setupServiceRecord(&key, relation, 0, service);
	// find the corresponding service record
	BTreeIterator iterator;
	BTreeIterate(&iterator, registry.services);
	ServiceRecord const * record;
	if(BTreeIteratorSeek(&iterator, &key)) {
		do {
			record = BTreeIteratorPeekItem(&iterator);
			if(record->service == service) {
				break;
			}
		} while(BTreeIteratorNext(&iterator));
	}
	BTreeIteratorEnd(&iterator);
	// TODO: this will fail if the B-tree is modified concurrently,
	// invalidating the record pointer.
	ASSERT(BTreeDelete(registry.services, record, 0) == BTREE_DELETED)
	ReleaseService(service);

	btreeFreeServiceRecord(&key, 0);
}


void RelationRemoveAllServices(RelationTable const * relation)
{
	ServiceRecord key;
	setupServiceRecord(&key, relation, 0, 0);
	ServiceRecord record;
	while(BTreeGetItem(registry.services, &key, &record)) {
	ASSERT(BTreeDelete(registry.services, &record, 0) == BTREE_DELETED)
		ReleaseService(record.service);
	}
	btreeFreeServiceRecord(&key, 0);
	// record is shallow-copied by BTreeGetItem() and does not need deallocation
}


size32 RegistryNRelations(void)
{
	return BTreeNItems(registry.relationBTree);
}


size32 RegistryNServices(void)
{
	return BTreeNItems(registry.services);
}


RelationTable const * FindRelationTable(Atom form, size8 nColumns, byte const atomTypes[])
{
	// make a copy to maintain const correctness
	byte atomTypesCopy[nColumns];
	CopyMemory(atomTypes, atomTypesCopy, nColumns);
	RelationTable key = {
		.form = form,
		.nColumns = nColumns,
		.atomTypes = atomTypesCopy
	};
	// the B-tree item nust be a pointer to the key structure
	RelationTable *keyPtr = &key;
	RelationTable ** relationPtr = BTreePeekItem(registry.relationBTree, &keyPtr);
	if(relationPtr)
		return *relationPtr;
	else
		return 0;
}


void RegistryIterate(RelationTable const * table, RegistryIterator * iterator)
{
	iterator->table = table;
	BTreeIterate(&(iterator->btreeIterator), registry.services);
}


ServiceRecord const * RegistryIteratorPeekRecord(RegistryIterator const * iterator)
{
	return BTreeIteratorPeekItem(&(iterator->btreeIterator));
}


bool RegistryIteratorNext(RegistryIterator * iterator)
{
	ServiceRecord key = {
		.relation = iterator->table,
		.parameterIO = 0,
		.service = 0
	};
	bool foundItem;
	if(BTreeIteratorBeforeFirst(&iterator->btreeIterator)) {
		foundItem = BTreeIteratorSeek(&(iterator->btreeIterator), &key);
	}
	else
		foundItem = BTreeIteratorNext(&(iterator->btreeIterator));

	if(foundItem) {
		ServiceRecord const * btreeRecord = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(compareServiceRecords(btreeRecord, &key) == 0)
			return true;		
	}
	return false;
}


void RegistryIteratorEnd(RegistryIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}


Service * RegistryFindService(RelationTable const * relation, byte const parameterIO[])
{
	ServiceRecord key;
	setupServiceRecord(&key, relation, parameterIO, 0);

	BTreeIterator iterator;
	BTreeIterate(&iterator, registry.services);
	Service * service = 0;
	if(BTreeIteratorSeek(&iterator, &key)) {
		ServiceRecord * record = BTreeIteratorPeekItem(&iterator);
		service = record->service;
	}
	BTreeIteratorEnd(&iterator);
	btreeFreeServiceRecord(&key, 0);
	return service;
}


void PrintServiceRecord(ServiceRecord const * record)
{
	TypedTuple * parameters = CreateTypedTuple(record->service->nArguments);
	for(index8 i = 0; i < record->service->nArguments; i++) {
		TypedAtom parameter = CreateTypedAtom(
			AT_PARAMETER,
			(Atom) {
				.parameter = {
					.number = i + 1,
					.atomType =	record->relation->atomTypes[i],
					.io = record->parameterIO[i]
				}
			}
		);
		TypedTupleSetElement(parameters, i, parameter);
	}
	PrintFormActorsAsFormula(record->relation->form, parameters);
	FreeTypedTuple(parameters);
	PrintCString(" => ");
	PrintService(record->service);
}


static void btreePrintCallback(void const * item)
{
	PrintServiceRecord((ServiceRecord const *) item);
	PrintChar('\n');
}


void RelationTableDump(RelationTable const * table)
{
	// find a service for enumerating all tuples
	byte parameterIO[table->nColumns];
	for(index8 i = 0; i < table->nColumns; i++)
		parameterIO[i] = PARAMETER_OUT;
	Service const * service = RegistryFindService(table, parameterIO);
	
	PrintF("Table %u columns\n", table->nColumns);

	Atom arguments[table->nColumns];
	ServiceContext * context = ServiceCreateContext(service, arguments);
	size32 nTuples = 0;
	while(ServiceCall(context)) {
		// TODO: we should probably not print the full representaiton
		// of identified atoms, as it triggers repeated queries
		PrintTuple(table->atomTypes, arguments, table->nColumns);
		PrintChar('\n');
		nTuples++;
	}
	ServiceFreeContext(context);
	PrintF("%u tuples\n", nTuples);
}


void RegistryDumpServices(void)
{
	BTreeTraversal(registry.services, &btreePrintCallback);
}

