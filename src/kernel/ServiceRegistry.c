
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


/**
 * The registry for both relations and their associated services.
 * We store RelationTable entries in a pool for stable allocation.
 */
struct {
	void * relationPool;
	// B-tree for lookup of relations by form
	BTree * relationBTree;
	// B-tree storing (relation, service) pairs
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
	if(relation->form.hash > 2)
		IFactRelease(relation->form);
	Free(relation->atomTypes);
	PoolFreeItem(registry.relationPool, relation);
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
	registry.relationBTree = CreatePool(sizeof(RelationTable));
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
		0
	);
}


void FreeRegistry(void)
{
	BTreeFree(registry.services);
	BTreeFree(registry.relationBTree);
	FreePool(registry.relationPool);
}


RelationTable const * CreateRelationTable(
	RelationTableProvider * provider, Atom form, size8 nColumns, byte const atomTypes[])
{
	RelationTable * relation = PoolAllocate(registry.relationPool);
	relation->form = form;
	relation->nColumns = nColumns;
	relation->atomTypes = Allocate(nColumns);
	CopyMemory(atomTypes, relation->atomTypes, nColumns);
	if(provider)
		relation->data = provider->createTable(nColumns, atomTypes);
	else
		relation->data = 0;
	// add to registry
	ASSERT(BTreeInsert(registry.relationBTree, &relation) == BTREE_INSERTED)
	return relation;
}


void RemoveRelationTable(RelationTable const * relation)
{
	ASSERT(BTreeDelete(registry.relationBTree, &relation))
	PoolFreeItem(registry.relationPool, (RelationTable *) relation);
}


void RelationAddService(RelationTable const * relation, byte const parameterIO[], Service * service)
{
	ServiceRecord record = {
		.relation = relation,
		.parameterIO = Allocate(relation->nColumns),
		.service = service
	};
	CopyMemory(parameterIO, record.parameterIO, relation->nColumns);
	BTreeInsert(registry.services, &record);
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
	RelationTable ** relationPtr = BTreePeekItem(registry.relationBTree, &key);
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


Service const * RegistryFindService(RelationTable const * relation, byte const parameterIO[])
{
	RegistryIterator iterator;
	RegistryIterate(relation, &iterator);
	Service * service = 0;
	if(RegistryIteratorNext(&iterator)) {
		service = RegistryIteratorPeekRecord(&iterator)->service;
		// the service must be unique
		ASSERT(!RegistryIteratorNext(&iterator))
	}
	RegistryIteratorEnd(&iterator);
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


void RegistryDump(void)
{
	BTreeTraversal(registry.services, &btreePrintCallback);
}

