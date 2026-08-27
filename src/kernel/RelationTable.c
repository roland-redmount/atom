
#include "kernel/Relation.h"
#include "kernel/RelationTable.h"
#include "kernel/RelationTableRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"
#include "memory/allocator.h"


RelationTable * CreateRelationTable(
	Relation const * relation, RelationTableProvider const * provider,
	index8 const indexColumns[])
{
	ASSERT(provider)
	// NOTE: pool allocation would be preferable
	RelationTable * table = Allocate(sizeof(RelationTable));
	table->relation = relation;
	AcquireRelation(relation);
	table->provider = provider;
	table->referenceCount = 1;
	table->isCore = false;
	// not readable by createStorage() below, which is what produces it
	table->storage = 0;

	table->indexColumns = Allocate(relation->nColumns);
	if(indexColumns)
		CopyMemory(indexColumns, table->indexColumns, relation->nColumns);
	else {
		// use the identity order
		for(index8 i = 0; i < relation->nColumns; i++)
			table->indexColumns[i] = i;
	}

	table->storage = provider->createStorage(table);
	RelationTableRegistryAdd(table);
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
	Free(table->indexColumns);
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

	RelationTableRegistryRemove(table);
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
