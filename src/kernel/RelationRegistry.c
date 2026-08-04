
#include "btree/btree.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"


/**
 * B-tree for lookup of relations by form, stores RelationTable * pointers as items.
 */
static BTree * relationRegistry;


static int8 compareRelations(RelationTable const * relation, RelationTable const * relationOrKey)
{
	// First compare forms
	if(relation->form.hash < relationOrKey->form.hash)
		return -1;
	else if(relation->form.hash > relationOrKey->form.hash)
		return 1;
	else {
		// then compare atom types
		if(!relationOrKey->atomTypes)
			return 0;
		ASSERT(relation->atomTypes)
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


void SetupRelationRegistry(void)
{
	// The lookup B-tree stores pointers to RelationTable records
	relationRegistry = BTreeCreate(
		sizeof(RelationTable *),
		btreeCompareRelations,
		btreeFreeRelation
	);
}


void FreeRelationRegistry(void)
{
	BTreeFree(relationRegistry);
}


void RelationRegistryAdd(RelationTable const * relation)
{
	ASSERT(BTreeInsert(relationRegistry, &relation) == BTREE_INSERTED)
}


void RelationRegistryRemove(RelationTable const * relation)
{
	ASSERT(BTreeDelete(relationRegistry, &relation, 0))
}


size32 RelationRegistryNTables(void)
{
	return BTreeNItems(relationRegistry);
}


RelationTable const * RelationRegistryFind(Atom form, size8 nColumns, byte const atomTypes[])
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
	RelationTable ** relationPtr = BTreePeekItem(relationRegistry, &keyPtr);
	if(relationPtr)
		return *relationPtr;
	else
		return 0;
}
