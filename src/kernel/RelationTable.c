
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"
#include "library/MachineService.h"
#include "memory/allocator.h"
#include "util/ResizingArray.h"


/**
 * B-tree for lookup of a RelationWriter by its Relation (signature).
 * Stores RelationWriter * pointers as items so that we can return
 * stable RelationWriter * pointers to callers.
 * NOTE: an alternative would be to return a RelationWriter copy.
 */
static BTree * tableRegistry;


RelationWriter * CreateRelationWriter(
	RelationSignature relation, StorageProvider const * provider
{
	// The relation must not already exist in the registry
	ASSERT(!FindRelationTable(relation))
	ASSERT(provider)
	size8 nColumns = TypeSignatureNAtomTypes(relation.typeSignature);

	// NOTE: pool allocation would be preferable
	RelationWriter * table = Allocate(sizeof(RelationWriter));
	table->relation = relation;
	table->provider = provider;

	// Add the relation table to the registry
	ASSERT(BTreeInsert(tableRegistry, &table) == BTREE_INSERTED)
	return table;
}


RelationWriter * FindOrCreateRelationTable(RelationSignature relation, StorageProvider const * provider)
{
	RelationWriter * table = FindRelationTable(relation);
	if(table)
		AcquireRelationTable(table);
	else
		table = CreateRelationWriter(relation, provider, 0);
	return table;		
}


static void removeTable(RelationWriter * table)
{
	removePrimitiveServices(table);
	ASSERT(BTreeDelete(tableRegistry, &table, 0) == BTREE_DELETED)
	table->provider->free(table->storage);
	ReleaseRelation(table->relation);
	Free(table);
}


void DropRelationWriter(RelationWriter * writer)
{
	ASSERT(false);
}

/**
 * This compares RelationTables by comparing the corresponding relations.
 */
static int8 btreeCompareTables(void const * item, void const * itemOrKey, size32 itemSize)
{
	RelationWriter * table = *((RelationWriter * const *) item);
	RelationWriter * tableOrKey = *((RelationWriter * const *) itemOrKey);
	return CompareRelations(table->relation, tableOrKey->relation);
}


void SetupRelationTableRegistry(void)
{
	tableRegistry = BTreeCreate(sizeof(RelationWriter *), btreeCompareTables, 0);
}


void FreeRelationTableRegistry(void)
{
	BTreeFree(tableRegistry);
}


RelationWriter * FindRelationTable(RelationSignature relation)
{
	// the B-tree item is a RelationWriter * pointer
	RelationWriter key = {.relation = relation};
	RelationWriter * keyPtr = &key;
	RelationWriter ** tablePtr = BTreePeekItem(tableRegistry, &keyPtr);
	if(tablePtr)
		return *tablePtr;
	else
		return 0;
}


size32 NumberOfRelationTables(void)
{
	return BTreeNItems(tableRegistry);
}

