
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


void RelationRegistryIterate(Atom form, RelationIterator * iterator)
{
	iterator->form = form;
	BTreeIterate(&(iterator->btreeIterator), relationRegistry);
}


bool RelationIteratorNext(RelationIterator * iterator)
{
	// A key without atom types matches every table for the form
	RelationTable key = {
		.form = iterator->form,
		.nColumns = 0,
		.atomTypes = 0
	};
	RelationTable * keyPtr = &key;

	bool foundItem;
	if(BTreeIteratorBeforeFirst(&(iterator->btreeIterator)))
		foundItem = BTreeIteratorSeek(&(iterator->btreeIterator), &keyPtr);
	else
		foundItem = BTreeIteratorNext(&(iterator->btreeIterator));

	if(foundItem) {
		RelationTable * const * btreeItem = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(compareRelations(*btreeItem, &key) == 0)
			return true;
	}
	return false;
}


RelationTable const * RelationIteratorGet(RelationIterator const * iterator)
{
	return *((RelationTable * const *) BTreeIteratorPeekItem(&(iterator->btreeIterator)));
}


void RelationIteratorEnd(RelationIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}
