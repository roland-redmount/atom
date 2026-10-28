
#include "kernel/MixedTypeRelation.h"
#include "lang/TermForm.h"
#include "memory/allocator.h"


/**
 * Supply the input arguments to the service at the current dispatch position.
 */
static void beginService(MixedTypeRelation * mixedRelation)
{
	size8 arity = mixedRelation->tuple->nAtoms;
	ServiceRecord const * serviceRecord = DispatchIteratorPeekServiceRecord(
		&(mixedRelation->impl.concat.dispatchIterator));
	Atom arguments[arity];
	for(index8 i = 0; i < arity; i++)
		arguments[i] = TypedTupleGetAtom(
			mixedRelation->impl.concat.queryActors, mixedRelation->impl.concat.permutation[i]);
	ServiceIterate(serviceRecord, arguments, &(mixedRelation->impl.concat.serviceIterator));
	mixedRelation->impl.concat.isReadingService = true;
	mixedRelation->impl.concat.nServices++;
}


static void endService(MixedTypeRelation * mixedRelation)
{
	ServiceIteratorEnd(&(mixedRelation->impl.concat.serviceIterator));
	mixedRelation->impl.concat.isReadingService = false;
}


/**
 * Copy the arguments of the current service into the tuple of the relation, which is in
 * query actor order and carries the column types of that service.
 */
static void copyResultToTypedTuple(MixedTypeRelation * mixedRelation)
{
	for(index8 i = 0; i < mixedRelation->tuple->nAtoms; i++)
		TypedTupleSetElement(
			mixedRelation->tuple,
			mixedRelation->impl.concat.permutation[i],
			ServiceIteratorGetElement(&(mixedRelation->impl.concat.serviceIterator), i)
		);
}


/**
 * Get the next tuple (if any) from a MIXED_TYPE_CONCAT relation 
 */
static bool concatNext(MixedTypeRelation * mixedRelation)
{
	while(!mixedRelation->impl.concat.isExhausted) {
		if(!mixedRelation->impl.concat.isReadingService) {
			// Look for next service
			if(!DispatchIteratorNext(&(mixedRelation->impl.concat.dispatchIterator))) {
				mixedRelation->impl.concat.isExhausted = true;
				break;
			}
			beginService(mixedRelation);
		}
		// Call the current service
		if(ServiceIteratorNext(&(mixedRelation->impl.concat.serviceIterator))) {
			copyResultToTypedTuple(mixedRelation);
			return true;
		}
		else {
			// This service is exhausted, and must not be called again
			endService(mixedRelation);
		}
	}
	return false;
}


MixedTypeRelation * CreateConcatRelation(FormulaView query)
{
	ASSERT(IsRelationForm(query.form))
	TypedTuple const * queryActors = query.actors;
	size8 arity = queryActors->nAtoms;

	MixedTypeRelation * mixedRelation = Allocate(sizeof(MixedTypeRelation));
	mixedRelation->type = MIXED_TYPE_CONCAT;
	mixedRelation->form = query.form;
	mixedRelation->tuple = CreateTypedTuple(arity);
	mixedRelation->impl.concat.queryActors = queryActors;

	ParameterizeQuery(query, &(mixedRelation->impl.concat.parameterizedQuery));
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
		if(mixedRelation->impl.concat.isReadingService)
			endService(mixedRelation);
		DispatchIteratorEnd(&(mixedRelation->impl.concat.dispatchIterator));
		break;

	default:
		ASSERT(false)
		break;
	}
	FreeTypedTuple(mixedRelation->tuple);
	Free(mixedRelation);
}
