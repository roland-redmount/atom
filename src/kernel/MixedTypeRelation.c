
#include "kernel/MixedTypeRelation.h"
#include "lang/TermForm.h"
#include "memory/allocator.h"


/**
 * Bind the arguments of the service at the current dispatch position and open its
 * context. Every query actor is copied into the arguments tuple, which binds the query
 * constants to the input parameters of the service; the service overwrites the arguments
 * taken by its output parameters.
 */
static void openService(MixedTypeRelation * relation)
{
	size8 arity = relation->tuple->nAtoms;
	relation->impl.concat.service = *DispatchIteratorPeekService(
		&(relation->impl.concat.dispatchIterator));

	for(index8 i = 0; i < arity; i++)
		relation->impl.concat.arguments[i] = TypedTupleGetAtom(
			relation->impl.concat.queryActors, relation->impl.concat.permutation[i]);

	relation->impl.concat.context = OperatorCreateContext(
		relation->impl.concat.service.op, relation->impl.concat.arguments);
}


/**
 * Copy the arguments of the current service into the tuple of the relation, which is in
 * query actor order and carries the column types of that service.
 */
static void gatherTuple(MixedTypeRelation * relation)
{
	byte const * atomTypes = relation->impl.concat.service.relation->atomTypes;
	for(index8 i = 0; i < relation->tuple->nAtoms; i++)
		TypedTupleSetElement(
			relation->tuple,
			relation->impl.concat.permutation[i],
			CreateTypedAtom(atomTypes[i], relation->impl.concat.arguments[i])
		);
}


/**
 * Test whether the current tuple of the relation satisfies the equality constraints
 * of the query, if any. Any euqliaty-constrained arguments always have the same atom type.
 */
static bool tupleSatisfiesConstraints(MixedTypeRelation const * relation)
{
	index8 const * equalityMap = relation->impl.concat.equalityMap;
	if(!equalityMap)
		return true;	// no constraints to check

	for(index8 i = 0; i < relation->tuple->nAtoms; i++) {
		if(equalityMap[i] == i)
			continue;
		if(CompareAtoms(
			TypedTupleGetAtom(relation->tuple, i),
			TypedTupleGetAtom(relation->tuple, equalityMap[i])))
			return false;
	}
	return true;
}

/**
 * Get the next tuple (if any) from a MIXED_TYPE_CONCAT relation 
 */
static bool concatNext(MixedTypeRelation * relation)
{
	while(!relation->impl.concat.isExhausted) {
		if(!relation->impl.concat.context) {
			if(!DispatchIteratorNext(&(relation->impl.concat.dispatchIterator))) {
				relation->impl.concat.isExhausted = true;
				break;
			}
			openService(relation);
		}

		if(OperatorCall(relation->impl.concat.context)) {
			gatherTuple(relation);
			if(tupleSatisfiesConstraints(relation))
				return true;
		}
		else {
			// This service is exhausted, and its context must not be called again
			OperatorFreeContext(relation->impl.concat.context);
			relation->impl.concat.context = 0;
		}
	}
	return false;
}


MixedTypeRelation * CreateConcatRelation(Atom queryTermForm, TypedTuple const * queryActors)
{
	ASSERT(IsTermForm(queryTermForm))
	size8 arity = queryActors->nAtoms;

	MixedTypeRelation * relation = Allocate(sizeof(MixedTypeRelation));
	relation->type = MIXED_TYPE_CONCAT;
	relation->termForm = queryTermForm;
	relation->tuple = CreateTypedTuple(arity);
	relation->impl.concat.queryActors = queryActors;
	relation->impl.concat.context = 0;
	relation->impl.concat.isExhausted = false;

	// The arguments and permutation arrays share one allocation, the arguments first
	// so that they keep the alignment of an Atom
	relation->impl.concat.arguments = Allocate(arity * (sizeof(Atom) + sizeof(index8)));
	relation->impl.concat.permutation = (index8 *) (relation->impl.concat.arguments + arity);

	// Create the equalityMap only if there are repeated variables
	index8 equalityMap[arity];
	if(QueryEqualityMap(queryActors, equalityMap)) {
		relation->impl.concat.equalityMap = Allocate(arity * sizeof(index8));
		CopyMemory(equalityMap, relation->impl.concat.equalityMap, arity * sizeof(index8));
	}
	else
		relation->impl.concat.equalityMap = 0;

	DispatchQueryIterate(
		queryTermForm, queryActors, relation->impl.concat.permutation,
		&(relation->impl.concat.dispatchIterator));
	return relation;
}


bool MixedTypeRelationNext(MixedTypeRelation * relation)
{
	switch(relation->type) {
	case MIXED_TYPE_CONCAT:
		return concatNext(relation);

	default:
		// MIXED_TYPE_FORMULA is not implemented
		ASSERT(false)
		return false;
	}
}


TypedTuple const * MixedTypeRelationPeekTuple(MixedTypeRelation const * relation)
{
	return relation->tuple;
}


void FreeMixedTypeRelation(MixedTypeRelation * relation)
{
	switch(relation->type) {
	case MIXED_TYPE_CONCAT:
		if(relation->impl.concat.context)
			OperatorFreeContext(relation->impl.concat.context);
		DispatchIteratorEnd(&(relation->impl.concat.dispatchIterator));
		Free(relation->impl.concat.arguments);
		if(relation->impl.concat.equalityMap)
			Free(relation->impl.concat.equalityMap);
		break;

	default:
		ASSERT(false)
		break;
	}
	FreeTypedTuple(relation->tuple);
	Free(relation);
}
