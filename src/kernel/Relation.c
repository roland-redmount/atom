
#include "kernel/ifact.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"
#include "lang/TermForm.h"
#include "memory/allocator.h"


Relation const * CreateRelationBootstrap(
	Atom termForm, Atom predicateForm, size8 nColumns, byte const atomTypes[])
{
	ASSERT(nColumns <= RELATION_MAX_ARITY)
	// NOTE: pool allocation would be preferable
	Relation * relation = Allocate(sizeof(Relation));
	SetMemory(relation, sizeof(Relation), 0);
	relation->termForm = termForm;
	IFactAcquire(termForm);
	relation->predicateForm = predicateForm;
	IFactAcquire(predicateForm);
	relation->ownsForm = true;
	relation->nColumns = nColumns;
	CopyMemory(atomTypes, relation->atomTypes, nColumns);
	relation->referenceCount = 1;

	RelationRegistryAdd(relation);
	return relation;
}


Relation const * CreateRelation(Atom termForm, size8 nColumns, byte const atomTypes[])
{
	return CreateRelationBootstrap(
		termForm, TermFormGetPredicateForm(termForm), nColumns, atomTypes);
}


Relation const * FindOrCreateRelation(Atom termForm, size8 nColumns, byte const atomTypes[])
{
	Relation const * relation = RelationRegistryFind(termForm, nColumns, atomTypes);
	if(relation) {
		AcquireRelation(relation);
		return relation;
	}
	return CreateRelation(termForm, nColumns, atomTypes);
}


void AcquireRelation(Relation const * relation)
{
	((Relation *) relation)->referenceCount++;
}


void ReleaseRelation(Relation const * relation)
{
	Relation * mutableRelation = (Relation *) relation;
	mutableRelation->referenceCount--;
	if(mutableRelation->referenceCount > 0)
		return;

	RelationRegistryRemove(relation);
	if(relation->ownsForm) {
		IFactRelease(relation->termForm);
		IFactRelease(relation->predicateForm);
	}
	Free(mutableRelation);
}


void RelationReleaseForm(Relation const * relation)
{
	ASSERT(relation->ownsForm)
	// clear the flag first, as the release may retract tuples from this relation
	((Relation *) relation)->ownsForm = false;
	IFactRelease(relation->termForm);
	IFactRelease(relation->predicateForm);
}
