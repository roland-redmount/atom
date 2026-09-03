
#include "kernel/MixedTypeRelation.h"
#include "kernel/Parameter.h"
#include "lang/TermForm.h"
#include "lang/Variable.h"
#include "memory/allocator.h"


/**
 * Detect repeated variables. If queryActors[i] is a variable, equalityMap[i] i set to the index
 * of the first variable in queryActors that equals queryActors[i].
 * Returns true if any repeated variables were found.
 */
static bool queryVariableMap(TypedTuple const * queryActors, index8 equalityMap[])
{
	bool hasRepeatedVariable = false;
	for(index8 i = 0; i < queryActors->nAtoms; i++) {
		TypedAtom queryAtom = TypedTupleGetElement(queryActors, i);
		equalityMap[i] = i;
		if(queryAtom.type == AT_VARIABLE) {
			// check for repeated variables
			for(index8 j = 0; j < i; j++) {
				TypedAtom previousAtom = TypedTupleGetElement(queryActors, j);
				if(previousAtom.type == AT_VARIABLE && SameVariable(queryAtom.atom, previousAtom.atom)) {
					equalityMap[i] = j;
					hasRepeatedVariable = true;
					break;
				}
			}
		}
	}
	return hasRepeatedVariable;
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
	relation->impl.concat.nServices++;
}


/**
 * Copy the arguments of the current service into the tuple of the relation, which is in
 * query actor order and carries the column types of that service.
 */
static void gatherTuple(MixedTypeRelation * relation)
{
	byte const * atomTypes = relation->impl.concat.service.relation->typeSignature.atomTypes;
	for(index8 i = 0; i < relation->tuple->nAtoms; i++)
		TypedTupleSetElement(
			relation->tuple,
			relation->impl.concat.permutation[i],
			CreateTypedAtom(atomTypes[i], relation->impl.concat.arguments[i])
		);
}


/**
 * Test whether the current tuple of the relation satisfies the equality constraints
 * of the query, if any. Equality-constrained arguments must always have the same atom type.
 */
static bool tupleSatisfiesConstraints(MixedTypeRelation const * relation)
{
	index8 const * variableMap = relation->impl.concat.variableMap;
	if(!variableMap)
		return true;	// no constraints to check

	for(index8 i = 0; i < relation->tuple->nAtoms; i++) {
		if(variableMap[i] == i)
			continue;
		if(!SameTypedAtoms(
			TypedTupleGetElement(relation->tuple, i),
			TypedTupleGetElement(relation->tuple, variableMap[i])))
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

	// The arguments, query parameters and permutation arrays share one allocation, the
	// atoms first so that they keep the alignment of an Atom
	relation->impl.concat.arguments = Allocate(
		arity * (2 * sizeof(Atom) + sizeof(index8)));
	relation->impl.concat.queryParameters = relation->impl.concat.arguments + arity;
	relation->impl.concat.permutation =
		(index8 *) (relation->impl.concat.queryParameters + arity);

	// Create the variable map only if there are repeated variables
	index8 variableMap[arity];
	if(queryVariableMap(queryActors, variableMap)) {
		relation->impl.concat.variableMap = Allocate(arity * sizeof(index8));
		CopyMemory(variableMap, relation->impl.concat.variableMap, arity * sizeof(index8));
	}

	ActorsToParameters(queryActors, relation->impl.concat.queryParameters);
	DispatchIterate(
		queryTermForm, relation->impl.concat.queryParameters, arity, DISPATCH_MATCH_EXACT,
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


size32 MixedTypeRelationNServices(MixedTypeRelation const * relation)
{
	ASSERT(relation->type == MIXED_TYPE_CONCAT)
	return relation->impl.concat.nServices;
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
		if(relation->impl.concat.variableMap)
			Free(relation->impl.concat.variableMap);
		break;

	default:
		ASSERT(false)
		break;
	}
	FreeTypedTuple(relation->tuple);
	Free(relation);
}
