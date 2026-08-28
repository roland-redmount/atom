
#include "kernel/Relation.h"
#include "kernel/RelationTable.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"
#include "memory/allocator.h"


/**
 * B-tree for lookup of a RelationTable by its Relation (signature).
 * Stores RelationTable * pointers as items so that we can return
 * stable RelationTable * pointers to callers.
 * NOTE: an alternative would be to return a RelationTable copy.
 */
static BTree * tableRegistry;


RelationTable * CreateRelationTable(
	Relation const * relation, RelationTableProvider const * provider,
	index8 const indexColumns[])
{
	// The relation must not already exist in the registry
	ASSERT(!RelationTableRegistryFind(relation))
	ASSERT(provider)

	// NOTE: pool allocation would be preferable
	RelationTable * table = Allocate(sizeof(RelationTable));
	table->relation = relation;
	AcquireRelation(relation);
	table->provider = provider;
	// The table registery owns 1 reference to the table
	table->referenceCount = 1;
	table->isCore = false;
	// not readable by createStorage() below, which is what produces it
	table->storage = 0;

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
	table->storage = provider->createStorage(table);

	// Add the relation table to the registry
	ASSERT(BTreeInsert(tableRegistry, &table) == BTREE_INSERTED)
	// Let the storage provider register services
	provider->registerServices(table);

	return table;
}


void AcquireRelationTable(RelationTable * table)
{
	table->referenceCount++;
}


void ReleaseRelationTable(RelationTable * table)
{
	table->referenceCount--;
	if(table->referenceCount > 0)
		return;

	table->provider->free(table);
	ReleaseRelation(table->relation);
	Free(table);
}


void DropRelationTable(RelationTable * table)
{
	ASSERT(RelationTableNRows(table) == 0)

	// Removing a service releases the reference the registry holds to its operator, which
	// may free the operator and so release this table. The creation reference released at
	// the end keeps the table alive until then, and the reference the table holds to its
	// relation keeps the relation alive across the loop inside ServiceRegistryRemoveAll().
	ServiceRegistryRemoveAll(table->relation);

	ASSERT(BTreeDelete(tableRegistry, &table, 0) == BTREE_DELETED)
	ReleaseRelationTable(table);
}


byte RelationTableAddTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	Relation const * relation = table->relation;
	byte result = table->provider->addTuple(table, tuple, idPosition);
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
	return table->provider->numberOfTuples(table);
}


byte RelationTableRemoveTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	Relation const * relation = table->relation;
	byte result = table->provider->removeTuple(table, tuple, idPosition);
	if(result == TUPLE_REMOVED) {
		for(index32 i = 0; i < relation->nColumns; i++) {
			if((i + 1) != idPosition)
				ReleaseTypedAtom(CreateTypedAtom(relation->typeSignature.atomTypes[i], tuple[i]));
		}
	}
	return result;
}


/**
 * This compares RelationTables by the relation pointers.
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

