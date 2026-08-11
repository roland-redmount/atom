
#include "btree/btree.h"
#include "kernel/ifact.h"
#include "kernel/RelationTable.h"
#include "kernel/operator.h"
#include "lang/TermForm.h"
#include "lang/TypedAtom.h"
#include "memory/allocator.h"


RelationTable const * CreateRelationTableBootstrap(
	RelationTableProvider * provider, Atom termForm, Atom predicateForm,
	size8 nColumns, byte const atomTypes[], index8 const indexColumns[])
{
	// NOTE: pool allocation would be preferable
	RelationTable * relation = Allocate(sizeof(RelationTable));
	relation->provider = provider;
	relation->termForm = termForm;
	IFactAcquire(termForm);
	relation->predicateForm = predicateForm;
	IFactAcquire(predicateForm);
	relation->ownsForm = true;
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


RelationTable const * CreateRelationTable(
	RelationTableProvider * provider, Atom form, size8 nColumns, byte const atomTypes[], index8 const indexColumns[])
{
	return CreateRelationTableBootstrap(
		provider, form, TermFormGetPredicateForm(form), nColumns, atomTypes, indexColumns);
}


void RelationTableReleaseForm(RelationTable const * table)
{
	ASSERT(table->ownsForm)
	// clear the flag first, as the release may retract tuples from this table
	((RelationTable *) table)->ownsForm = false;
	IFactRelease(table->termForm);
	IFactRelease(table->predicateForm);
}


void FreeRelationTable(RelationTable const * table)
{
	// a computed relation has no provider, and so no stored tuples
	if(table->provider) {
		ASSERT(RelationTableNRows(table) == 0)
		table->provider->free(table->storage);
	}
	if(table->ownsForm) {
		IFactRelease(table->termForm);
		IFactRelease(table->predicateForm);
	}
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


byte RelationTableRemoveTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition)
{
	byte result = table->provider->removeTuple(table->storage, tuple, idPosition);
	if(result == TUPLE_REMOVED) {
		for(index32 i = 0; i < table->nColumns; i++) {
			if((i + 1) != idPosition)
				ReleaseTypedAtom(CreateTypedAtom(table->atomTypes[i], tuple[i]));
		}
	}
	return result;
}
