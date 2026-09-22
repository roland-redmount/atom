
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "lang/ClauseForm.h"
#include "lang/FormPermutation.h"
#include "lang/formula.h"
#include "lang/SubstitutionList.h"
#include "lang/unification.h"


void ParameterizeQuery(FormulaView query, ParameterizedQuery * parameterizedQuery)
{
	parameterizedQuery->termForm = query.form;
	parameterizedQuery->arity = query.actors->nAtoms;
	ActorsToParameters(query.actors, parameterizedQuery->parameters);
}


void PrintParameterizedQuery(ParameterizedQuery const * parameterizedQuery)
{
	TypedTuple * tuple = CreateTypedTupleFromTuple(
		AT_PARAMETER, parameterizedQuery->parameters, parameterizedQuery->arity);
	PrintFormActorsAsFormula(parameterizedQuery->termForm, tuple);
	FreeTypedTuple(tuple);
}


bool DispatchParameterIOMatch(byte queryIO, byte serviceIO, int matchMode)
{
	if(queryIO == serviceIO)
		return true;
	return (matchMode == DISPATCH_MATCH_RELAXED) && (serviceIO == PARAMETER_OUT);
}


/**
 * Test whether query parameters matches a service signature (typeSignature, ioSignature),
 * permuted according to the given permutation array (0-based indices).
 *
 * 1) Query parameter atom types in must equal the typeSignature, or be absent.
 * 2) With matchMode = DISPATCH_MATCH_EXACT the IO direction of each query parameter
 *    must agree with ioSignature; with matchMode = DISPATCH_MATCH_RELAXED, only
 *    output parameters must match ioSignature outputs.
 * 3) A parameter (identified by its number) occurring at several positions must match
 *    the same type in typeSignature at all positions.
 *
 * Returns true if the query matches.
 */
static bool signatureQueryTupleMatch(
	TypeSignature typeSignature, IOSignature ioSignature, Atom const queryParameters[],
	size8 nParameters, int matchMode, index8 const permutation[])
{
	// iterate over query parameters
	for(index8 i = 0; i < nParameters; i++) {
		Atom queryParameter = queryParameters[permutation[i]];
		// test IO direction
		if(!DispatchParameterIOMatch(
			queryParameter.parameter.io, ioSignature.parameterIO[i], matchMode))
			return false;
		// test parameter type
		byte serviceParameterType = typeSignature.atomTypes[i];
		if(queryParameter.parameter.atomType
			&& (queryParameter.parameter.atomType != serviceParameterType))
			return false;
		// An earlier occurence of this parameter must have matched the same type,
		// or no single atom could satisfy the query
		for(index8 j = 0; j < i; j++) {
			if((queryParameters[permutation[j]].parameter.number == queryParameter.parameter.number)
				&& (typeSignature.atomTypes[j] != serviceParameterType))
				return false;
		}
	}
	return true;
}


/**
 * Enumerate all possible argument permutations for the given service's term form
 * and test each for a match against the query parameters.
 */
static bool permutationMatch(
	Service service, Atom const queryParameters[], size8 nParameters, int matchMode, index8 permutation[])
{
	// iterate over all permutations of the form
	Atom predicateForm = TermFormGetPredicateForm(service.relation.termForm);
	FormIterator * iter = CreateFormIterator(predicateForm);
	bool match = false;
	do {
		GetTuplePermutation(iter, permutation);
		if(signatureQueryTupleMatch(
			service.relation.typeSignature, service.ioSignature, queryParameters, nParameters, matchMode,
			permutation))
		{
			match = true;
			break;
		}
	} while(NextFormPermutation(iter));
	FreeFormIterator(iter);
	return match;
}


void DispatchIterate(
	ParameterizedQuery const * query, int matchMode, index8 permutation[], DispatchIterator * iterator)
{
	ASSERT(IsTermForm(query->termForm))
	SetMemory(iterator, sizeof(DispatchIterator), 0);
	iterator->queryParameters = query->parameters;
	iterator->nParameters = query->arity;
	iterator->matchMode = matchMode;
	iterator->permutation = permutation;
	iterator->inRelation = false;
	// Iterate over relations matching the term form.
	RelationRegistryIterate(query->termForm, &(iterator->relationIterator));
}


bool DispatchIteratorNext(DispatchIterator * iterator)
{
	while(true) {
		if(!iterator->inRelation) {
			// find next relation matching the query term form
			if(!RelationIteratorNext(&(iterator->relationIterator)))
				return false;
			ServiceRegistryIterate(
				RelationIteratorGet(&(iterator->relationIterator)),
				&(iterator->serviceIterator)
			);
			iterator->inRelation = true;
		}

		// Iterate over candidate services for the current relation
		while(ServiceIteratorNext(&(iterator->serviceIterator))) {
			iterator->serviceRecord = ServiceIteratorPeekRecord(&(iterator->serviceIterator));
			if(permutationMatch(
				iterator->serviceRecord->service,
				iterator->queryParameters, iterator->nParameters, iterator->matchMode,
				iterator->permutation))
			{
				// For DISPATCH_MATCH_EXACT, there can be only one match per relation,
				// since we cannot have two services with the same IO signagure.
				// For DISPATCH_MATCH_RELAXED, we arbitrarily pick the first match for each relation.
				ServiceIteratorEnd(&(iterator->serviceIterator));
				iterator->inRelation = false;
				return true;
			}
		}
		ServiceIteratorEnd(&(iterator->serviceIterator));
		iterator->inRelation = false;
	}
}


ServiceRecord const * DispatchIteratorPeekServiceRecord(DispatchIterator const * iterator)
{
	return iterator->serviceRecord;
}


void DispatchIteratorEnd(DispatchIterator * iterator)
{
	if(iterator->inRelation) {
		ServiceIteratorEnd(&(iterator->serviceIterator));
		iterator->inRelation = false;
	}
	RelationIteratorEnd(&(iterator->relationIterator));
}


static bool isExcludedCandidate(TypeSignature candidateSignature, TypeSignature const excludedSignatures[], size8 nExcluded)
{
	for(index8 i = 0; i < nExcluded; i++) {
		if(SameTypeSignatures(candidateSignature, excludedSignatures[i]))
			return true;
	}
	return false;
}


DispatchResult DispatchParameterizedQuery(
	ParameterizedQuery const * query, int matchMode, Service * service, index8 permutation[],
	TypeSignature const excludedSignatures[], size8 nExcluded, bool * hasNextMatch)
{
	// QUESTION: what is this assert good for? (added by Claude)
	ASSERT(!nExcluded || (query->arity <= RELATION_MAX_ARITY))

	// The iterator overwrites its permutation array on every match, so iterate into
	// a scratch array to avoid clobbering the returned permutation.
	index8 candidatePermutation[query->arity];
	DispatchIterator iterator;
	DispatchIterate(query, matchMode, candidatePermutation, &iterator);

	DispatchResult result = DISPATCH_NOT_FOUND;
	if(hasNextMatch)
		*hasNextMatch = false;

	while(DispatchIteratorNext(&iterator)) {
		if(isExcludedCandidate(iterator.serviceRecord->service.relation.typeSignature, excludedSignatures, nExcluded))
			continue;
		if(result != DISPATCH_NOT_FOUND) {
			// There are additional matches beyond the one we return
			*hasNextMatch = true;
			break;
		}
		Service candidateService = iterator.serviceRecord->service;
		result = RelationIsStale(candidateService.relation) ? DISPATCH_FOUND_STALE : DISPATCH_FOUND;
		// copy the service struct and its permutation to the caller
		*service = candidateService;
		CopyMemory(candidatePermutation, permutation, query->arity * sizeof(index8));
		// without a hasNextMatch request we can stop at the first match;
		// else we continue to determine if there are additional matches
		if(hasNextMatch == 0)
			break;
	}
	DispatchIteratorEnd(&iterator);

	return result;
}


DispatchResult DispatchQuery(FormulaView query, Service * service, index8 permutation[])
{
	ParameterizedQuery parameterizedQuery = {
		.termForm = query.form,
		.arity = query.actors->nAtoms,
	};
	ActorsToParameters(query.actors, parameterizedQuery.parameters);

	return DispatchParameterizedQuery(
		&parameterizedQuery, DISPATCH_MATCH_EXACT, service, permutation, 0, 0, 0);
}


DispatchResult DispatchQueryFormula(Atom queryTerm, Service * service, index8 permutation[])
{
	FormulaView term = FormulaGetView(queryTerm);
	return DispatchQuery(term, service, permutation);
}

