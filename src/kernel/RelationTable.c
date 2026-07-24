
#include "btree/btree.h"
#include "kernel/RelationTable.h"
#include "kernel/service.h"
#include "lang/TypedAtom.h"
#include "memory/allocator.h"
#include "memory/pool.h"


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
