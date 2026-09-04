
#include "btree/btree.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"


/**
 * B-tree for lookup of relations by form, stores Relation * pointers as items.
 */
static BTree * relationRegistry;


static int8 btreeCompareRelations(void const * item, void const * itemOrKey, size32 itemSize)
{
	return CompareRelations(*((Relation * const *) item), *((Relation * const *) itemOrKey));
}


void SetupRelationRegistry(void)
{
	// The lookup B-tree stores pointers to Relation records. The registry holds no
	// reference to a relation, and so has no free-item callback: a relation removes
	// itself from the registry when its last reference goes.
	relationRegistry = BTreeCreate(
		sizeof(Relation *),
		btreeCompareRelations,
		0
	);
}


void FreeRelationRegistry(void)
{
	BTreeFree(relationRegistry);
}


void RelationRegistryAdd(Relation const * relation)
{
	ASSERT(BTreeInsert(relationRegistry, &relation) == BTREE_INSERTED)
}


void RelationRegistryRemove(Relation const * relation)
{
	ASSERT(BTreeDelete(relationRegistry, &relation, 0) == BTREE_DELETED)
}


size32 RelationRegistryNRelations(void)
{
	return BTreeNItems(relationRegistry);
}


Relation const * RelationRegistryFind(Atom form, size8 nColumns, TypeSignature typeSignature)
{
	ASSERT(nColumns <= RELATION_MAX_ARITY)
	Relation key = {
		.termForm = form,
		.nColumns = nColumns,
		.typeSignature = typeSignature
	};

	// the B-tree item must be a pointer to the key structure
	Relation * keyPtr = &key;
	Relation ** relationPtr = BTreePeekItem(relationRegistry, &keyPtr);
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
	// A key without atom types matches every relation for the form
	Relation key = {
		.termForm = iterator->form,
		.nColumns = 0,
		.typeSignature = {.atomTypes = {0}},
	};
	Relation * keyPtr = &key;

	bool foundItem;
	if(BTreeIteratorBeforeFirst(&(iterator->btreeIterator)))
		foundItem = BTreeIteratorSeek(&(iterator->btreeIterator), &keyPtr);
	else
		foundItem = BTreeIteratorNext(&(iterator->btreeIterator));

	if(foundItem) {
		Relation * const * btreeItem = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(CompareRelations(*btreeItem, &key) == 0)
			return true;
	}
	return false;
}


Relation const * RelationIteratorGet(RelationIterator const * iterator)
{
	return *((Relation * const *) BTreeIteratorPeekItem(&(iterator->btreeIterator)));
}


void RelationIteratorEnd(RelationIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}
