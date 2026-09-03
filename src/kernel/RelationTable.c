
#include "kernel/Relation.h"
#include "kernel/RelationTable.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"
#include "memory/allocator.h"
#include "util/ResizingArray.h"


/**
 * B-tree for lookup of a RelationTable by its Relation (signature).
 * Stores RelationTable * pointers as items so that we can return
 * stable RelationTable * pointers to callers.
 * NOTE: an alternative would be to return a RelationTable copy.
 */
static BTree * tableRegistry;

/**
 * Callback used by StorageProvider.createStorage()
 */
static void providerCreateOperatorCallback(
	void * data, MachineOperatorProvider * operatorProvider, void * providerData, size32 contextSize,
	IOSignature ioSignature)
{
	RelationTable * table = data;
	Operator * op = CreateMachineOperator(
		table->relation->nColumns, table->indexColumns, operatorProvider, providerData,
		contextSize);
	CreateService(
		table->relation, ioSignature, op, SERVICE_PRIMITIVE);
}


RelationTable * CreateRelationTable(
	Relation const * relation, StorageProvider const * provider, index8 const indexColumns[])
{
	// The relation must not already exist in the registry
	ASSERT(!RelationTableRegistryFind(relation))
	ASSERT(provider)

	// NOTE: pool allocation would be preferable
	RelationTable * table = Allocate(sizeof(RelationTable));
	table->relation = relation;
	AcquireRelation(relation);
	table->provider = provider;
	table->referenceCount = 1;		// the caller owns this reference

	// setup index column array
	SetMemory(&table->indexColumns, RELATION_MAX_ARITY, 0);
	if(indexColumns)
		CopyMemory(indexColumns, table->indexColumns, relation->nColumns);
	else {
		// use the identity order
		for(index8 i = 0; i < relation->nColumns; i++)
			table->indexColumns[i] = i;
	}
	// Call the storage provider to prepare storage for the table
	table->storage = provider->createStorage(
		table->indexColumns, relation->nColumns, table, providerCreateOperatorCallback);
	// Add the relation table to the registry
	ASSERT(BTreeInsert(tableRegistry, &table) == BTREE_INSERTED)
	return table;
}


RelationTable * FindOrCreateRelationTable(Relation const * relation, StorageProvider const * provider)
{
	RelationTable * table = RelationTableRegistryFind(relation);
	if(table)
		AcquireRelationTable(table);
	else
		table = CreateRelationTable(relation, provider, 0);
	return table;		
}


void AcquireRelationTable(RelationTable * table)
{
	table->referenceCount++;
}


/**
 * Remove the primitive services of this table.
 */
static void removePrimitiveServices(RelationTable * table)
{
	// TODO: here we must reset the iterator after every removal,
	// since RemoveService() alters the service registry B-tree
	// and invalidates any pointers obtained. See CreateService()
	Service const * service;
	do {
		ServiceIterator iterator;
		ServiceRegistryIterate(table->relation, &iterator);
		service = 0;
		while(ServiceIteratorNext(&iterator)) {
			Service const * candidate = ServiceIteratorPeekService(&iterator);
			if(candidate->kind == SERVICE_PRIMITIVE) {
				service = candidate;
				break;
			}
		}
		ServiceIteratorEnd(&iterator);
		if(service)
			RemoveService(service->relation, service->op);
	} while(service);
}

/**
 * A RelationTable is "stale" (can be deallocated) when (1) it has zero references,
 * (2) it contains zero rows, and (3) no service depends on any of its primitive services
 */
static bool tableIsStale(RelationTable const * table)
{
	if(table->referenceCount > 0 || RelationTableNRows(table) > 0)
		return false;
		
	bool hasDependentOperator = false;
	ServiceIterator serviceIterator;
	ServiceRegistryIterate(table->relation, &serviceIterator);
	while(ServiceIteratorNext(&serviceIterator)) {
		Service const * service = ServiceIteratorPeekService(&serviceIterator);
		if(service->kind == SERVICE_PRIMITIVE && ServiceHasDependents(service)) {
			hasDependentOperator = true;
			break;
		}
	}
	ServiceIteratorEnd(&serviceIterator);

	return !hasDependentOperator;
}


static void removeTable(RelationTable * table)
{
	removePrimitiveServices(table);
	ASSERT(BTreeDelete(tableRegistry, &table, 0) == BTREE_DELETED)
	table->provider->free(table->storage);
	ReleaseRelation(table->relation);
	Free(table);
}


void ReleaseRelationTable(RelationTable * table)
{
	table->referenceCount--;
	CheckRelationTable(table);
}


void CheckRelationTable(RelationTable * table)
{
	if(tableIsStale(table))
		removeTable(table);
}



byte RelationTableAddTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	Relation const * relation = table->relation;
	byte result = table->provider->addTuple(table->storage, tuple, idPosition);
	if(result == TUPLE_ADDED) {
		for(index8 i = 0; i < relation->nColumns; i++) {
			if(i + 1 != idPosition)
				AcquireAtom(tuple[i], relation->typeSignature.atomTypes[i]);
		}
	}
	return result;
}


size32 RelationTableNRows(RelationTable const * table)
{
	return table->provider->numberOfTuples(table->storage);
}


byte RelationTableRemoveTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	Relation const * relation = table->relation;
	byte result = table->provider->removeTuple(table->storage, tuple, idPosition);
	if(result == TUPLE_REMOVED) {
		for(index32 i = 0; i < relation->nColumns; i++) {
			if((i + 1) != idPosition)
				ReleaseTypedAtom(CreateTypedAtom(relation->typeSignature.atomTypes[i], tuple[i]));
		}
	}
	CheckRelationTable((RelationTable *) table);
	return result;
}


/**
 * This compares RelationTables by the Relation * pointers.
 * TODO: it seems better to use CompareRelations() ?
 */
static int8 btreeCompareTables(void const * item, void const * itemOrKey, size32 itemSize)
{
	Relation const * relation = (*(RelationTable * const *) item)->relation;
	Relation const * relationOrKey = (*(RelationTable * const *) itemOrKey)->relation;
	if(relation < relationOrKey)
		return -1;
	if(relation > relationOrKey)
		return 1;
	return 0;
}


void SetupRelationTableRegistry(void)
{
	// The registry holds no reference to a table, and so has no free-item callback:
	// a table removes itself from the registry when it is dropped.
	tableRegistry = BTreeCreate(sizeof(RelationTable *), btreeCompareTables, 0);
}


void FreeRelationTableRegistry(void)
{
	BTreeFree(tableRegistry);
}


RelationTable * RelationTableRegistryFind(Relation const * relation)
{
	// the B-tree item is a pointer to a table, so the key must be one too
	RelationTable key = {.relation = relation};
	RelationTable * keyPtr = &key;
	RelationTable ** tablePtr = BTreePeekItem(tableRegistry, &keyPtr);
	if(tablePtr)
		return *tablePtr;
	else
		return 0;
}


size32 RelationTableRegistryNTables(void)
{
	return BTreeNItems(tableRegistry);
}

