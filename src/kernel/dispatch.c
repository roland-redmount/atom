
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/Parameter.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/ClauseForm.h"
#include "lang/FormPermutation.h"
#include "lang/Formula.h"
#include "lang/SubstitutionList.h"
#include "lang/unification.h"

/**
 * Test whether a generalized query matches a service signature, permuted according to the
 * given permutation array (0-based indices). Both sides are parameters, so matching is
 * signature against signature:
 *
 * 1) The direction of each query parameter must agree with the service parameter, and its
 *    atom type must equal the type of the service column, or be absent, which is the case
 *    of an output whose type is not known yet.
 * 2) A parameter occurring at several positions of the query denotes one atom, and so
 *    must match service parameters of the same type. A query generalized from actors
 *    numbers every position separately and never has such a repeat; a rule body term
 *    does, which is what a CONSTRAIN operator is built on.
 *
 * Returns true if the query matches.
 */
static bool signatureQueryTupleMatch(
	byte const atomTypes[], byte const parameterIO[], Atom const queryParameters[],
	size8 nParameters, index8 const permutation[])
{
	// iterate over query parameters
	for(index8 i = 0; i < nParameters; i++) {
		Atom parameter = queryParameters[permutation[i]];
		byte serviceParameterType = atomTypes[i];

		if(parameter.parameter.io != parameterIO[i])
			return false;
		if(parameter.parameter.atomType
			&& (parameter.parameter.atomType != serviceParameterType))
			return false;

		// An earlier occurence of this parameter must have matched the same type,
		// or no single atom could satisfy the query
		for(index8 j = 0; j < i; j++) {
			if((queryParameters[permutation[j]].parameter.number == parameter.parameter.number)
				&& (atomTypes[j] != serviceParameterType))
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
	Atom predicateForm, byte const atomTypes[], byte const parameterIO[],
	Atom const queryParameters[], size8 nParameters, index8 permutation[])
{
	// iterate over all permutations of the form
	FormIterator * iter = CreateFormIterator(predicateForm);
	bool match = false;
	do {
		GetTuplePermutation(iter, permutation);
		if(signatureQueryTupleMatch(
			atomTypes, parameterIO, queryParameters, nParameters, permutation)) {
			match = true;
			break;
		}
	} while(NextFormPermutation(iter));
	FreeFormIterator(iter);
	return match;
}


void DispatchIterate(
	Atom queryTermForm, Atom const queryParameters[], size8 nParameters,
	index8 permutation[], DispatchIterator * iterator)
{
	ASSERT(IsTermForm(queryTermForm))

	iterator->queryParameters = queryParameters;
	iterator->nParameters = nParameters;
	iterator->permutation = permutation;
	iterator->inRelation = false;
#ifdef DEBUG
	iterator->previousMatchRelation = 0;
#endif
	// Iterate over relations matching the term form. Since a term form carries a sign,
	// a query for (! even x) only reaches relations for the negated predicate.
	// NOTE: this iteration order must be deterministic, as the compiler
	// identifies a choice point by the position of its match in this sequence.
	RelationRegistryIterate(queryTermForm, &(iterator->relationIterator));
}


bool DispatchIteratorNext(DispatchIterator * iterator)
{
	while(true) {
		if(!iterator->inRelation) {
			if(!RelationIteratorNext(&(iterator->relationIterator)))
				return false;
			ServiceRegistryIterate(
				RelationIteratorGet(&(iterator->relationIterator)), &(iterator->serviceIterator));
			iterator->inRelation = true;
		}

		// Iterate over candidate services for the relation table
		// TODO: this is inefficient, would be better to test once if the relation table
		// atom types are compatible with the query, and only then iterate over services.
		RelationTable const * relation = RelationIteratorGet(&(iterator->relationIterator));
		while(ServiceIteratorNext(&(iterator->serviceIterator))) {
			Service const * currentService = ServiceIteratorPeekService(&(iterator->serviceIterator));
			if(permutationMatch(
				relation->predicateForm, relation->atomTypes, currentService->parameterIO,
				iterator->queryParameters, iterator->nParameters, iterator->permutation))
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


bool DispatchGeneralizedQuery(
	Atom queryTermForm, Atom const queryParameters[], size8 nParameters, Service * service,
	index8 permutation[], size8 nSkip, bool * hasNextMatch)
{
	// The iterator overwrites its permutation array on every match, so iterate into
	// a scratch array to avoid clobbering the returned permutation.
	index8 candidatePermutation[nParameters];
	DispatchIterator iterator;
	DispatchIterate(
		queryTermForm, queryParameters, nParameters, candidatePermutation, &iterator);

	bool match = false;
	// Number of matches seen so far, whether skipped or returned
	size8 nMatches = 0;
	if(hasNextMatch)
		*hasNextMatch = false;

	while(DispatchIteratorNext(&iterator)) {
		if(match) {
			// An additional match exists beyond the one we return
			*hasNextMatch = true;
			break;
		}
		if(nMatches++ >= nSkip) {
			match = true;
			// copy the service struct and its permutation to the caller
			*service = *DispatchIteratorPeekService(&iterator);
			CopyMemory(candidatePermutation, permutation, nParameters * sizeof(index8));
			// without a hasNextMatch request we can stop at the first match
			if(hasNextMatch == 0)
				break;
		}
	}
	DispatchIteratorEnd(&iterator);

	return match;
}


bool DispatchQuery(
	Atom queryTermForm, TypedTuple const * queryActors, Service * service, index8 permutation[])
{
	// Dispatch the query type: the equality constraint of a repeated actor is not part
	// of what a service provides, and is applied to the tuples an answer is read from
	size8 arity = queryActors->nAtoms;
	Atom queryParameters[arity];
	GetQueryParameters(queryActors, queryParameters);

	return DispatchGeneralizedQuery(
		queryTermForm, queryParameters, arity, service, permutation, 0, 0);
}


bool DispatchQueryFormula(Formula * queryTerm, Service * service, index8 * permutation)
{
	return DispatchQuery(queryTerm->form, queryTerm->actors, service, permutation);
}

