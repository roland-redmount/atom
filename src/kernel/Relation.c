
#include "kernel/ifact.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"
#include "lang/TermForm.h"
#include "memory/allocator.h"
#include "util/hashing.h"


TypeSignature CreateTypeSignature(byte const atomTypes[], size8 nColumns)
{
	ASSERT(nColumns <= RELATION_MAX_ARITY)
	TypeSignature typeSignature = {.atomTypes = {0}};
	CopyMemory(atomTypes, typeSignature.atomTypes, nColumns);
	return typeSignature;
}


bool SameTypeSignatures(TypeSignature signature1, TypeSignature signature2)
{
	return CompareMemory(signature1.atomTypes, signature2.atomTypes, RELATION_MAX_ARITY) == 0;
}


Relation const * CreateRelationBootstrap(
	Atom termForm, Atom predicateForm, size8 nColumns, TypeSignature typeSignature)
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
	relation->typeSignature = typeSignature;
	relation->referenceCount = 1;

	RelationRegistryAdd(relation);
	return relation;
}


Relation const * CreateRelation(Atom termForm, size8 nColumns, TypeSignature typeSignature)
{
	return CreateRelationBootstrap(
		termForm, TermFormGetPredicateForm(termForm), nColumns, typeSignature);
}


Relation const * FindOrCreateRelation(Atom termForm, size8 nColumns, TypeSignature typeSignature)
{
	Relation const * relation = RelationRegistryFind(termForm, nColumns, typeSignature);
	if(relation) {
		AcquireRelation(relation);
		return relation;
	}
	return CreateRelation(termForm, nColumns, typeSignature);
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


data64 RelationHash(Relation const * relation, data64 initialHash)
{
	data64 hash = initialHash;
	// hash the form and types
	hash = DJB2DoubleHashAdd(&relation->termForm.hash, sizeof(data64), initialHash);
	hash = DJB2DoubleHashAdd(&relation->typeSignature, relation->nColumns, hash);
	return hash;
}
