
#include "kernel/Relation.h"
#include "kernel/RelationTable.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"
#include "library/MachineService.h"
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
 * Callback used by StorageProvider.createStorage().
 * Creates a machine operator and registers a primitive Service for it.
 */
static void providerCreateOperatorCallback(
	void * data, MachineOperatorSpec operatorSpec, IOSignature ioSignature)
{
	RelationTable * table = data;
	// The operator has a pointer to this table to notify on removal
	operatorSpec.relationTable = table;
	Operator * op = CreateMachineOperator(table->nColumns, table->indexColumns, operatorSpec);

	// We must permute the IOSignature to match the Service order
	IOSignature serviceIOSignature = {.parameterIO = {0}};
	for(index8 i = 0; i < table->nColumns; i++)
		serviceIOSignature.parameterIO[table->indexColumns[i]] = ioSignature.parameterIO[i];
	
	CreateService(table->relation, serviceIOSignature, op);
}


RelationTable * CreateRelationTable(
	Relation relation, StorageProvider const * provider, index8 const indexColumns[])
{
	// The relation must not already exist in the registry
	ASSERT(!FindRelationTable(relation))
	ASSERT(provider)
	size8 nColumns = TypeSignatureNAtomTypes(relation.typeSignature);

	// NOTE: pool allocation would be preferable
	RelationTable * table = Allocate(sizeof(RelationTable));
	table->relation = relation;
	table->nColumns = nColumns;
	AcquireRelation(relation);
	table->provider = provider;
	table->referenceCount = 1;		// the caller owns this reference

	// setup index column array
	if(indexColumns)
		CopyMemory(indexColumns, table->indexColumns, nColumns);
	else {
		// use the identity order
		for(index8 i = 0; i < nColumns; i++)
			table->indexColumns[i] = i;
	}
	// Call the storage provider to prepare storage for the table
	table->storage = provider->createStorage(nColumns, table, providerCreateOperatorCallback);
	// Add the relation table to the registry
	ASSERT(BTreeInsert(tableRegistry, &table) == BTREE_INSERTED)
	return table;
}


RelationTable * FindOrCreateRelationTable(Relation relation, StorageProvider const * provider)
{
	RelationTable * table = FindRelationTable(relation);
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
 * Check whether the given operator refers to the table.
 */
static bool isTableOperator(RelationTable const * table, Operator const * op)
{
	return (op->type == OPERATOR_MACHINE) && (op->impl.machine.spec.relationTable == table);
}

/**
 * Remove the primitive services of this table.
 */
static void removePrimitiveServices(RelationTable * table)
{
	Service const * service;
	do {
		ServiceIterator iterator;
		ServiceRegistryIterate(table->relation, &iterator);
		service = 0;
		while(ServiceIteratorNext(&iterator)) {
			Service const * candidate = ServiceIteratorPeekService(&iterator);
			if(isTableOperator(table, candidate->op)) {
				service = candidate;
				break;
			}
		}
		// Close the iterator, since RemoveService() alters the service registry B-tree
		ServiceIteratorEnd(&iterator);
		if(service) {
			service->op->impl.machine.spec.relationTable = 0;
			RemoveService(service->relation, service->op);
		}
	} while(service);
}

/**
 * A RelationTable is "stale" (can be deallocated) when (1) it has zero references,
 * (2) it contains zero rows, and (3) no service referencing this table has dependents.
 * 
 * NOTE: actually I think (1) and (3) are sufficient; such as table is unreachable for
 * either reading or writing, and could be removed (and tuples deallocated).
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
		// An operator with parents must have dependents, since the root operator
		// of every operator graph must be associated with a Service.
		if(isTableOperator(table, service->op) && service->op->nParents > 0) {
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



byte RelationTableAddTuple(RelationTable * table, Atom const tuple[], uint8 idPosition)
{
	// Permute tuple to the provider's order
	Atom providerTuple[table->nColumns];
	for(index8 i = 0; i < table->nColumns; i++)
		providerTuple[i] = tuple[table->indexColumns[i]];
	index8 providerIdPositon = idPosition ? table->indexColumns[idPosition - 1] + 1 : 0;

	byte result = table->provider->addTuple(table->storage, providerTuple, providerIdPositon);

	// Acquire atoms
	if(result == TUPLE_ADDED) {
		for(index8 i = 0; i < table->nColumns; i++) {
			if(i + 1 != idPosition)
				AcquireAtom(tuple[i], table->relation.typeSignature.atomTypes[i]);
		}
	}
	return result;
}


size32 RelationTableNRows(RelationTable const * table)
{
	return table->provider->numberOfTuples(table->storage);
}


byte RelationTableRemoveTuple(RelationTable * table, Atom const tuple[], uint8 idPosition)
{
	// Permute tuple to the provider's order
	Atom providerTuple[table->nColumns];
	for(index8 i = 0; i < table->nColumns; i++)
		providerTuple[i] = tuple[table->indexColumns[i]];
	index8 providerIdPositon = idPosition ? table->indexColumns[idPosition - 1] + 1 : 0;

	byte result = table->provider->removeTuple(table->storage, providerTuple, providerIdPositon);

	// Release atoms
	if(result == TUPLE_REMOVED) {
		for(index32 i = 0; i < table->nColumns; i++) {
			if((i + 1) != idPosition)
				ReleaseTypedAtom(CreateTypedAtom(table->relation.typeSignature.atomTypes[i], tuple[i]));
		}
	}
	CheckRelationTable(table);
	return result;
}


/**
 * This compares RelationTables by comparing the corresponding relations.
 */
static int8 btreeCompareTables(void const * item, void const * itemOrKey, size32 itemSize)
{
	RelationTable * table = *((RelationTable * const *) item);
	RelationTable * tableOrKey = *((RelationTable * const *) itemOrKey);
	return CompareRelations(table->relation, tableOrKey->relation);
}


void SetupRelationTableRegistry(void)
{
	tableRegistry = BTreeCreate(sizeof(RelationTable *), btreeCompareTables, 0);
}


void FreeRelationTableRegistry(void)
{
	BTreeFree(tableRegistry);
}


RelationTable * FindRelationTable(Relation relation)
{
	// the B-tree item is a RelationTable * pointer
	RelationTable key = {.relation = relation};
	RelationTable * keyPtr = &key;
	RelationTable ** tablePtr = BTreePeekItem(tableRegistry, &keyPtr);
	if(tablePtr)
		return *tablePtr;
	else
		return 0;
}


size32 NumberOfRelationTables(void)
{
	return BTreeNItems(tableRegistry);
}

