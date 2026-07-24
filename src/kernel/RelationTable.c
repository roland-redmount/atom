
#include "btree/btree.h"
#include "kernel/RelationTable.h"
#include "kernel/service.h"
#include "lang/TypedAtom.h"
#include "memory/allocator.h"
#include "memory/pool.h"


byte RelationTableAddTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	byte result = table->provider->addTuple(table, tuple, idPosition);
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
	return table->provider->numberOfTuples(table);
}


byte RelationTableRemoveTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	byte result = table->provider->removeTuple(table, tuple);
	if(result == TUPLE_REMOVED) {
		for(index32 j = 0; j < table->nColumns; j++) {
			if((table->atomTypes[j] == AT_ID) && (j + 1 != idPosition))
				ReleaseTypedAtom((TypedAtom) {.type = table->atomTypes[j], .atom = tuple[j]});
		}
	}
	return result;
}
