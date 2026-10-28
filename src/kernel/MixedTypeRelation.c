
#include "kernel/MixedTypeRelation.h"
#include "lang/TermForm.h"
#include "memory/allocator.h"


/**
 * Bind the arguments of the service at the current dispatch position and create its
 * context. Every query actor is copied into the arguments tuple, which binds the query
 * constants to the input parameters of the service; the service overwrites the arguments
 * taken by its output parameters.
 */
static void createServiceContext(MixedTypeRelation * mixedRelation)
{
	size8 arity = mixedRelation->tuple->nAtoms;
	ServiceRecord const * serviceRecord = DispatchIteratorPeekServiceRecord(
		&(mixedRelation->impl.concat.dispatchIterator));

	// The service's operator takes one argument per distinct parameter; a repeated query
	// variable is copied once per column, to the same argument
	index8 * argumentMap = mixedRelation->impl.concat.argumentMap;
	EqualitySignatureGetArgumentMap(
		serviceRecord->service.equalitySignature, arity, argumentMap);
	for(index8 i = 0; i < arity; i++)
		mixedRelation->impl.concat.arguments[argumentMap[i]] = TypedTupleGetAtom(
			mixedRelation->impl.concat.queryActors, mixedRelation->impl.concat.permutation[i]);

	// CLAUDE: The service operator yields every column order of a fact whose form repeats
	// a role. The CANONICAL operator keeps one tuple per fact; see OPERATOR_CANONICAL.
	Operator * op = serviceRecord->op;
	index8 groupArguments[arity];
	size8 groupSizes[arity];
	size8 nGroups = ServiceGetExchangeableGroups(serviceRecord->service, groupArguments, groupSizes);
	if(nGroups > 0) {
		op = CreateCanonicalOperator(op, groupArguments, groupSizes, nGroups);
		mixedRelation->impl.concat.canonicalOperator = op;
	}
	mixedRelation->impl.concat.context = OperatorCreateContext(
		op, mixedRelation->impl.concat.arguments
	);
	mixedRelation->impl.concat.nServices++;
}


/* CLAUDE: Free the context of the current service, and the CANONICAL operator over the
   service operator, if any. */
static void freeServiceContext(MixedTypeRelation * mixedRelation)
{
	OperatorFreeContext(mixedRelation->impl.concat.context);
	mixedRelation->impl.concat.context = 0;
	if(mixedRelation->impl.concat.canonicalOperator) {
		CheckOperator(mixedRelation->impl.concat.canonicalOperator);
		mixedRelation->impl.concat.canonicalOperator = 0;
	}
}


/**
 * Copy the arguments of the current service into the tuple of the relation, which is in
 * query actor order and carries the column types of that service.
 */
static void copyResultToTypedTuple(MixedTypeRelation * mixedRelation)
{
	ServiceRecord const * serviceRecord = DispatchIteratorPeekServiceRecord(
		&(mixedRelation->impl.concat.dispatchIterator));
	TypeSignature typeSignature = serviceRecord->service.relation.typeSignature;
	for(index8 i = 0; i < mixedRelation->tuple->nAtoms; i++)
		TypedTupleSetElement(
			mixedRelation->tuple,
			mixedRelation->impl.concat.permutation[i],
			CreateTypedAtom(
				typeSignature.atomTypes[i],
				mixedRelation->impl.concat.arguments[mixedRelation->impl.concat.argumentMap[i]])
		);
}


/**
 * Get the next tuple (if any) from a MIXED_TYPE_CONCAT relation 
 */
static bool concatNext(MixedTypeRelation * mixedRelation)
{
	while(!mixedRelation->impl.concat.isExhausted) {
		if(!mixedRelation->impl.concat.context) {
			// Look for next service
			if(!DispatchIteratorNext(&(mixedRelation->impl.concat.dispatchIterator))) {
				mixedRelation->impl.concat.isExhausted = true;
				break;
			}
			createServiceContext(mixedRelation);
		}
		// Call the current service context
		if(OperatorCall(mixedRelation->impl.concat.context)) {
			copyResultToTypedTuple(mixedRelation);
			return true;
		}
		else {
			// This service is exhausted, and its context must not be called again
			freeServiceContext(mixedRelation);
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

	// The arguments and permutation arrays share one allocation, the atoms first
	// so that they keep the alignment of an Atom
	mixedRelation->impl.concat.arguments = Allocate(
		arity * (sizeof(Atom) + sizeof(index8)));
	mixedRelation->impl.concat.permutation =
		(index8 *) (mixedRelation->impl.concat.arguments + arity);

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
		if(mixedRelation->impl.concat.context)
			freeServiceContext(mixedRelation);
		DispatchIteratorEnd(&(mixedRelation->impl.concat.dispatchIterator));
		Free(mixedRelation->impl.concat.arguments);
		break;

	default:
		ASSERT(false)
		break;
	}
	FreeTypedTuple(mixedRelation->tuple);
	Free(mixedRelation);
}
