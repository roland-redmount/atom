
#include "btree/btree.h"
#include "kernel/RelationTableRegistry.h"


/**
 * B-tree for lookup of relation tables by relation, stores RelationTable * pointers as
 * items. A relation is interned by the relation registry, so ordering by relation pointer
 * orders by signature; see RelationRegistry.h
 */
static BTree * tableRegistry;


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


void RelationTableRegistryAdd(RelationTable const * table)
{
	ASSERT(!RelationTableRegistryFind(table->relation))
	ASSERT(BTreeInsert(tableRegistry, &table) == BTREE_INSERTED)
}


void RelationTableRegistryRemove(RelationTable const * table)
{
	ASSERT(BTreeDelete(tableRegistry, &table, 0) == BTREE_DELETED)
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
