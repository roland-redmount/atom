
#include "btree/btree.h"
#include "kernel/ifact.h"
#include "kernel/RelationTable.h"
#include "kernel/service.h"
#include "lang/TypedAtom.h"
#include "memory/allocator.h"


RelationTable const * CreateRelationTable(
	RelationTableProvider * provider, Atom form, size8 nColumns, byte const atomTypes[], index8 const indexColumns[])
{
	// NOTE: pool allocation would be preferable
	RelationTable * relation = Allocate(sizeof(RelationTable));
	relation->provider = provider;
	relation->form = form;
	IFactAcquire(form);
	relation->nColumns = nColumns;
	// NOTE: this seems like to many small allocs ...
	relation->atomTypes = Allocate(nColumns);
	CopyMemory(atomTypes, relation->atomTypes, nColumns);

	relation->indexColumns = Allocate(nColumns);
	if(indexColumns) {
		CopyMemory(indexColumns, relation->indexColumns, nColumns);
	}
	else {
		// use the identity order
		for(index8 i = 0; i < nColumns; i++)
			relation->indexColumns[i] = i;
	}
	
	if(provider)
		relation->storage = provider->createStorage(nColumns, atomTypes, relation->indexColumns);
	else
		relation->storage = 0;

	return relation;
}


void FreeRelationTable(RelationTable const * table)
{
	IFactRelease(table->form);
	Free(table->atomTypes);
	Free(table->indexColumns);
	Free((void *) table);
}


byte RelationTableAddTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	byte result = table->provider->addTuple(table->storage, tuple, idPosition);
	if(result == TUPLE_ADDED) {
		for(index8 i = 0; i < table->nColumns; i++) {
			if(i + 1 != idPosition)
				AcquireAtom(tuple[i], table->atomTypes[i]);
		}
	}
	return result;
}


size32 RelationTableNRows(RelationTable const * table)
{
	return table->provider->numberOfTuples(table->storage);
}


byte RelationTableRemoveTuple(RelationTable const * table, Atom const tuple[])
{
	byte result = table->provider->removeTuple(table->storage, tuple);
	if(result == TUPLE_REMOVED) {
		for(index32 i = 0; i < table->nColumns; i++) {
			ReleaseTypedAtom(CreateTypedAtom(table->atomTypes[i], tuple[i]));
		}
	}
	return result;
}


void RelationTableRemoveIFactTuples(RelationTable const * table, Atom idAtom, uint8 idPosition)
{
	table->provider->removeIFactTuples(table->storage, idAtom, idPosition);
}
