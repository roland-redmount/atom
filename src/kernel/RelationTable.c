
#include "kernel/RelationTable.h"


RelationTable CreateRelationTable(
	RelationTableProvider * provider, Atom form, size8 nColumns, byte const atomTypes[])
{
	RelationTable table;
	table.nColumns = nColumns;
	table.data = provider->createTable(form, nColumns, atomTypes);

	// TODO: store the table description in a registry for lookup later
	// This should also acquire a reference to the form atom

	return table;
}


bool RelationTableAddTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition)
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


RelationTable FindRelationTable(Atom form, byte const * atomTypes)
{
	// TODO:
	ASSERT(false);
	return (RelationTable) {0};
}


size32 RelationTableNRows(RelationTable const * table)
{
	return table->provider->numberOfTuples(table);
}


byte RelationTableRemoveTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	table->provider->removeTuple(table, tuple);
	for(index32 j = 0; j < table->nColumns; j++) {
		if(j + 1 != idPosition)
			ReleaseTypedAtom(tuple[j]);
	}
}
