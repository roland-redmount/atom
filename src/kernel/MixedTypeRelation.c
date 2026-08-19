
#include "kernel/MixedTypeRelation.h"
#include "kernel/Parameter.h"
#include "lang/TermForm.h"
#include "lang/Variable.h"
#include "memory/allocator.h"


/**
 * Test whether two query actors are one variable, which is the only way an actor denotes
 * the same atom at two positions of a query: any other actor stands for itself.
 *
 * NOTE: each occurence of the anonymous variable _ is a variable of its own,
 * which SameVariable() gives us.
 */
static bool isRepeatedVariable(TypedAtom first, TypedAtom second)
{
	if((first.type != AT_VARIABLE) || (second.type != AT_VARIABLE))
		return false;
	return SameVariable(first.atom, second.atom);
}


/**
 * Set equalityMap[i] to the index of the first query actor denoting the same atom as
 * actor i, which is i itself for an actor occurring once. Returns true if any actor is
 * repeated, which is the only case where a tuple can fail the constraint.
 *
 * This is the constraint the query type drops, as every actor obtains a parameter of its
 * own there; see GetQueryParameters().
 */
static bool queryEqualityMap(TypedTuple const * queryActors, index8 equalityMap[])
{
	bool hasRepeatedActor = false;
	for(index8 i = 0; i < queryActors->nAtoms; i++) {
		TypedAtom queryAtom = TypedTupleGetElement(queryActors, i);
		equalityMap[i] = i;
		for(index8 j = 0; j < i; j++) {
			if(isRepeatedVariable(queryAtom, TypedTupleGetElement(queryActors, j))) {
				equalityMap[i] = j;
				hasRepeatedActor = true;
				break;
			}
		}
	}
	return hasRepeatedActor;
}


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
		// The atom types are compared as well: dispatch matches the query type, which
		// says nothing about the columns of a repeated actor, and an atom of one type
		// may have the bit pattern of an atom of another
		if(!SameTypedAtoms(
			TypedTupleGetElement(relation->tuple, i),
			TypedTupleGetElement(relation->tuple, equalityMap[i])))
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
	if(queryEqualityMap(queryActors, equalityMap)) {
		relation->impl.concat.equalityMap = Allocate(arity * sizeof(index8));
		CopyMemory(equalityMap, relation->impl.concat.equalityMap, arity * sizeof(index8));
	}
	else
		relation->impl.concat.equalityMap = 0;

	// Dispatch the query type, and keep the tuple: the iterator reads it as it goes
	relation->impl.concat.queryParameters = CreateTypedTuple(arity);
	GetQueryParameters(queryActors, relation->impl.concat.queryParameters);
	DispatchIterate(
		queryTermForm, relation->impl.concat.queryParameters,
		relation->impl.concat.permutation, &(relation->impl.concat.dispatchIterator));
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
		FreeTypedTuple(relation->impl.concat.queryParameters);
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
