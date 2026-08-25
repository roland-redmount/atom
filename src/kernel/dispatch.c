
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/Parameter.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/ClauseForm.h"
#include "lang/FormPermutation.h"
#include "lang/formula.h"
#include "lang/SubstitutionList.h"
#include "lang/unification.h"

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
		if(queryParameter.parameter.io != ioSignature.parameterIO[i]) {
			if((matchMode != DISPATCH_MATCH_RELAXED) || (ioSignature.parameterIO[i] != PARAMETER_OUT))
				return false;
		}
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
 * Enumerate all possible argument permutations for the given form
 * and test each for a match against parametersList.
 * Returns true if a match is found.
 */
static bool permutationMatch(
	Atom predicateForm, TypeSignature typeSignature, IOSignature ioSignature,
	Atom const queryParameters[], size8 nParameters, int matchMode, index8 permutation[])
{
	// iterate over all permutations of the form
	FormIterator * iter = CreateFormIterator(predicateForm);
	bool match = false;
	do {
		GetTuplePermutation(iter, permutation);
		if(signatureQueryTupleMatch(
			typeSignature, ioSignature, queryParameters, nParameters, matchMode,
			permutation)) {
			match = true;
			break;
		}
	} while(NextFormPermutation(iter));
	FreeFormIterator(iter);
	return match;
}


void DispatchIterate(
	Atom queryTermForm, Atom const queryParameters[], size8 nParameters, int matchMode,
	index8 permutation[], DispatchIterator * iterator)
{
	ASSERT(IsTermForm(queryTermForm))

	iterator->queryParameters = queryParameters;
	iterator->nParameters = nParameters;
	iterator->matchMode = matchMode;
	iterator->permutation = permutation;
	iterator->inRelation = false;
#ifdef DEBUG
	iterator->previousMatchRelation = 0;
#endif
	// Iterate over relations matching the term form.
	RelationRegistryIterate(queryTermForm, &(iterator->relationIterator));
}


bool DispatchIteratorNext(DispatchIterator * iterator)
{
	while(true) {
		if(!iterator->inRelation) {
			// find next relation matching the query term form
			if(!RelationIteratorNext(&(iterator->relationIterator)))
				return false;
			ServiceRegistryIterate(
				RelationIteratorGet(&(iterator->relationIterator)), &(iterator->serviceIterator));
			iterator->inRelation = true;
		}

		// Iterate over candidate services for the current relation
		Relation const * relation = RelationIteratorGet(&(iterator->relationIterator));
		while(ServiceIteratorNext(&(iterator->serviceIterator))) {
			Service const * currentService = ServiceIteratorPeekService(&(iterator->serviceIterator));
			if(permutationMatch(
				relation->predicateForm, relation->typeSignature, currentService->ioSignature,
				iterator->queryParameters, iterator->nParameters, iterator->matchMode,
				iterator->permutation))
			{
				// Copy the service, as a pointer into the service registry is only
				// valid until the service iterator moves on.
				iterator->service = *currentService;
#ifdef DEBUG
				// There should only be one service per relation matching the query. 
				// A second match indicates a service that should never have been registered,
				// so that the service registry is corrupted. See ServiceRegistryAdd()
				ASSERT(relation != iterator->previousMatchRelation)
				iterator->previousMatchRelation = relation;
#endif
				// In "relaxed" matching mode, iteration ends after the first match
				if(iterator->matchMode == DISPATCH_MATCH_RELAXED) {
					ServiceIteratorEnd(&(iterator->serviceIterator));
					iterator->inRelation = false;
				}
				return true;
			}
		}
		ServiceIteratorEnd(&(iterator->serviceIterator));
		iterator->inRelation = false;
	}
}


Service const * DispatchIteratorPeekService(DispatchIterator const * iterator)
{
	return &(iterator->service);
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


bool DispatchParameterizedQuery(
	Atom queryTermForm, Atom const queryParameters[], size8 nParameters, int matchMode,
	Service * service, index8 permutation[],
	TypeSignature const excludedSignatures[], size8 nExcluded, bool * hasNextMatch)
{
	ASSERT(!nExcluded || (nParameters <= RELATION_MAX_ARITY))

	// The iterator overwrites its permutation array on every match, so iterate into
	// a scratch array to avoid clobbering the returned permutation.
	index8 candidatePermutation[nParameters];
	DispatchIterator iterator;
	DispatchIterate(
		queryTermForm, queryParameters, nParameters, matchMode, candidatePermutation,
		&iterator);

	bool match = false;
	if(hasNextMatch)
		*hasNextMatch = false;

	while(DispatchIteratorNext(&iterator)) {
		Service const * candidate = DispatchIteratorPeekService(&iterator);
		if(isExcludedCandidate(candidate->relation->typeSignature, excludedSignatures, nExcluded))
			continue;
		if(match) {
			// There are additional matches beyond the one we return
			*hasNextMatch = true;
			break;
		}
		match = true;
		// copy the service struct and its permutation to the caller
		*service = *candidate;
		CopyMemory(candidatePermutation, permutation, nParameters * sizeof(index8));
		// without a hasNextMatch request we can stop at the first match;
		// else we continue to determine if there are additional matches
		if(hasNextMatch == 0)
			break;
	}
	DispatchIteratorEnd(&iterator);

	return match;
}


bool DispatchQuery(FormulaView query, Service * service, index8 permutation[])
{
	size8 arity = query.actors->nAtoms;
	Atom queryParameters[arity];
	ActorsToParameters(query.actors, queryParameters);

	return DispatchParameterizedQuery(
		query.form, queryParameters, arity, DISPATCH_MATCH_EXACT, service, permutation,
		0, 0, 0);
}


bool DispatchQueryFormula(Atom queryTerm, Service * service, index8 * permutation)
{
	FormulaView term = FormulaGetView(queryTerm);
	return DispatchQuery(term, service, permutation);
}

