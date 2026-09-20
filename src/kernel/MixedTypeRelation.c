
#include "kernel/MixedTypeRelation.h"
#include "kernel/Parameter.h"
#include "lang/TermForm.h"
#include "lang/Variable.h"
#include "memory/allocator.h"


/**
 * Map repeated variables in the actors tuple. If actors[i] is a variable,
 * equalityMap[i] i set to the index of the first variable in queryActors that equals queryActors[i].
 * Returns true if any repeated variables were found.
 */
static bool repeatedVariablesMap(TypedTuple const * actors, index8 variableMap[])
{
	bool hasRepeatedVariable = false;
	for(index8 i = 0; i < actors->nAtoms; i++) {
		TypedAtom queryAtom = TypedTupleGetElement(actors, i);
		variableMap[i] = i;
		if(queryAtom.type == AT_VARIABLE) {
			// check for repeated variables
			for(index8 j = 0; j < i; j++) {
				TypedAtom previousAtom = TypedTupleGetElement(actors, j);
				if(previousAtom.type == AT_VARIABLE && SameVariable(queryAtom.atom, previousAtom.atom)) {
					variableMap[i] = j;
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
static void openService(MixedTypeRelation * mixedRelation)
{
	size8 arity = mixedRelation->tuple->nAtoms;
	mixedRelation->impl.concat.service = DispatchIteratorPeekService(
		&(mixedRelation->impl.concat.dispatchIterator));

	for(index8 i = 0; i < arity; i++)
		mixedRelation->impl.concat.arguments[i] = TypedTupleGetAtom(
			mixedRelation->impl.concat.queryActors, mixedRelation->impl.concat.permutation[i]);

	Operator * op = FindServiceOperator(mixedRelation->impl.concat.service);
	mixedRelation->impl.concat.context = OperatorCreateContext(
		op,	mixedRelation->impl.concat.arguments
	);
	mixedRelation->impl.concat.nServices++;
}


/**
 * Copy the arguments of the current service into the tuple of the relation, which is in
 * query actor order and carries the column types of that service.
 */
static void gatherTuple(MixedTypeRelation * mixedRelation)
{
	byte const * atomTypes = mixedRelation->impl.concat.service.relation.typeSignature.atomTypes;
	for(index8 i = 0; i < mixedRelation->tuple->nAtoms; i++)
		TypedTupleSetElement(
			mixedRelation->tuple,
			mixedRelation->impl.concat.permutation[i],
			CreateTypedAtom(atomTypes[i], mixedRelation->impl.concat.arguments[i])
		);
}


/**
 * Test whether the current tuple of the relation satisfies the equality constraints
 * of the query, if any. Equality-constrained arguments must always have the same atom type.
 */
static bool tupleSatisfiesConstraints(MixedTypeRelation const * mixedRelation)
{
	index8 const * variableMap = mixedRelation->impl.concat.variableMap;
	if(!variableMap)
		return true;	// no constraints to check

	for(index8 i = 0; i < mixedRelation->tuple->nAtoms; i++) {
		if(variableMap[i] == i)
			continue;
		if(!SameTypedAtoms(
			TypedTupleGetElement(mixedRelation->tuple, i),
			TypedTupleGetElement(mixedRelation->tuple, variableMap[i])))
			return false;
	}
	return true;
}

/**
 * Get the next tuple (if any) from a MIXED_TYPE_CONCAT relation 
 */
static bool concatNext(MixedTypeRelation * mixedRelation)
{
	while(!mixedRelation->impl.concat.isExhausted) {
		if(!mixedRelation->impl.concat.context) {
			if(!DispatchIteratorNext(&(mixedRelation->impl.concat.dispatchIterator))) {
				mixedRelation->impl.concat.isExhausted = true;
				break;
			}
			openService(mixedRelation);
		}

		if(OperatorCall(mixedRelation->impl.concat.context)) {
			gatherTuple(mixedRelation);
			if(tupleSatisfiesConstraints(mixedRelation))
				return true;
		}
		else {
			// This service is exhausted, and its context must not be called again
			OperatorFreeContext(mixedRelation->impl.concat.context);
			mixedRelation->impl.concat.context = 0;
		}
	}
	return false;
}


MixedTypeRelation * CreateConcatRelation(Atom queryTermForm, TypedTuple const * queryActors)
{
	ASSERT(IsTermForm(queryTermForm))
	size8 arity = queryActors->nAtoms;

	MixedTypeRelation * mixedRelation = Allocate(sizeof(MixedTypeRelation));
	mixedRelation->type = MIXED_TYPE_CONCAT;
	mixedRelation->termForm = queryTermForm;
	mixedRelation->tuple = CreateTypedTuple(arity);
	mixedRelation->impl.concat.queryActors = queryActors;

	// The arguments and permutation arrays share one allocation, the atoms first
	// so that they keep the alignment of an Atom
	mixedRelation->impl.concat.arguments = Allocate(
		arity * (sizeof(Atom) + sizeof(index8)));
	mixedRelation->impl.concat.permutation =
		(index8 *) (mixedRelation->impl.concat.arguments + arity);

	// Create the variable map only if there are repeated variables
	index8 variableMap[arity];
	if(repeatedVariablesMap(queryActors, variableMap)) {
		mixedRelation->impl.concat.variableMap = Allocate(arity * sizeof(index8));
		CopyMemory(variableMap, mixedRelation->impl.concat.variableMap, arity * sizeof(index8));
	}
	mixedRelation->impl.concat.parameterizedQuery.termForm = queryTermForm;
	mixedRelation->impl.concat.parameterizedQuery.arity = arity;
	ActorsToParameters(queryActors, mixedRelation->impl.concat.parameterizedQuery.parameters);
	DispatchIterate(
		&(mixedRelation->impl.concat.parameterizedQuery), DISPATCH_MATCH_EXACT,
		mixedRelation->impl.concat.permutation,
		&(mixedRelation->impl.concat.dispatchIterator)
	);
	return mixedRelation;
}


bool MixedTypeRelationNext(MixedTypeRelation * mixedRelation)
{
	switch(mixedRelation->type) {
	case MIXED_TYPE_CONCAT:
		return concatNext(mixedRelation);

	default:
		// MIXED_TYPE_FORMULA is not implemented
		ASSERT(false)
		return false;
	}
}


size32 MixedTypeRelationNServices(MixedTypeRelation const * mixedRelation)
{
	ASSERT(mixedRelation->type == MIXED_TYPE_CONCAT)
	return mixedRelation->impl.concat.nServices;
}


TypedTuple const * MixedTypeRelationPeekTuple(MixedTypeRelation const * mixedRelation)
{
	return mixedRelation->tuple;
}


void FreeMixedTypeRelation(MixedTypeRelation * mixedRelation)
{
	switch(mixedRelation->type) {
	case MIXED_TYPE_CONCAT:
		if(mixedRelation->impl.concat.context)
			OperatorFreeContext(mixedRelation->impl.concat.context);
		DispatchIteratorEnd(&(mixedRelation->impl.concat.dispatchIterator));
		Free(mixedRelation->impl.concat.arguments);
		if(mixedRelation->impl.concat.variableMap)
			Free(mixedRelation->impl.concat.variableMap);
		break;

	default:
		ASSERT(false)
		break;
	}
	FreeTypedTuple(mixedRelation->tuple);
	Free(mixedRelation);
}
