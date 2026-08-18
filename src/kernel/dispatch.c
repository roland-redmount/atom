
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
#include "lang/Variable.h"
#include "lang/unification.h"

/**
 * Test whether two query atoms denote the same atom, so that they can only match
 * service parameters of the same type. Only variables and parameters can do so;
 * any other atom is matched against the service parameter type directly.
 *
 * NOTE: each occurence of the anonymous variable _ is a variable of its own,
 * which SameVariable() gives us.
 */
static bool sameQueryAtom(TypedAtom first, TypedAtom second)
{
	if(first.type != second.type)
		return false;
	if(first.type == AT_VARIABLE)
		return SameVariable(first.atom, second.atom);
	if(first.type == AT_PARAMETER)
		return first.atom.parameter.number == second.atom.parameter.number;
	return false;
}


/**
 * Test whether a query tuple matches a service parameters list when permuted
 * according to the given permutation array (0-based indices). The query tuple
 * may contain parameters (used by the compiler). Matching rules are:
 *
 * 1) Each service input parameter must match (i) a query atom of the same type
 *    as the service parameter atom type, or (ii) a query input parameter
 *    of the same type or type == NONE.
 * 2) Each service output parameter must match (1) a query variable, or
 *    (ii) a query output parameter of the same type or type == NONE.
 * 3) A variable or parameter occurring at several positions of the query denotes
 *    one atom, and so must match service parameters of the same type.
 *
 * Returns true if the tuples match.
 */
static bool signatureQueryTupleMatch(
	byte const atomTypes[], byte const parameterIO[], TypedTuple const * queryActors, index8 const permutation[])
{
	// iterate over query tuple
	for(index8 i = 0; i < queryActors->nAtoms; i++) {
		TypedAtom queryAtom = TypedTupleGetElement(queryActors, permutation[i]);
		byte serviceParameterType = atomTypes[i];

		// An earlier occurence of this query atom must have matched the same type,
		// or no single atom could satisfy the query
		for(index8 j = 0; j < i; j++) {
			if(sameQueryAtom(queryAtom, TypedTupleGetElement(queryActors, permutation[j]))
				&& (atomTypes[j] != serviceParameterType))
				return false;
		}

		switch(parameterIO[i]) {
		case PARAMETER_IN:
			// service has an input parameter
			if(queryAtom.type == AT_PARAMETER) {
				// query has a parameter; IO direction must match
				if(queryAtom.atom.parameter.io != PARAMETER_IN)
					return false;
				// parameter atom type must match, or be absent
				byte queryParameterType = queryAtom.atom.parameter.atomType;
				if(queryParameterType && (queryParameterType != serviceParameterType))
					return false;
			}
			else {
				// query atom type must match the parameter type
				if(queryAtom.type != serviceParameterType)
					return false;
			}
			break;
		
		case PARAMETER_OUT:
			// service has an output parameter
			if(queryAtom.type == AT_PARAMETER) {
				if(queryAtom.atom.parameter.io != PARAMETER_OUT)
					return false;
				byte queryParameterType = queryAtom.atom.parameter.atomType;
				if(queryParameterType && (queryParameterType != serviceParameterType))
					return false;
			}
			else {
				if(queryAtom.type != AT_VARIABLE)
					return false;
			}
			break;
		}
	}
	return true;
}


bool QueryEqualityMap(TypedTuple const * queryActors, index8 equalityMap[])
{
	bool hasRepeatedActor = false;
	for(index8 i = 0; i < queryActors->nAtoms; i++) {
		TypedAtom queryAtom = TypedTupleGetElement(queryActors, i);
		equalityMap[i] = i;
		for(index8 j = 0; j < i; j++) {
			if(sameQueryAtom(queryAtom, TypedTupleGetElement(queryActors, j))) {
				equalityMap[i] = j;
				hasRepeatedActor = true;
				break;
			}
		}
	}
	return hasRepeatedActor;
}


/**
 * Enumerate all possible argument permutations for the given form
 * and test each for a match against parametersList.
 * Returns true if a match is found.
 */
static bool permutationMatch(
	Atom predicateForm, byte const atomTypes[], byte const parameterIO[],
	TypedTuple const * queryActors, index8 permutation[])
{
	// iterate over all permutations of the form
	FormIterator * iter = CreateFormIterator(predicateForm);
	bool match = false;
	do {
		GetTuplePermutation(iter, permutation);
		if(signatureQueryTupleMatch(atomTypes, parameterIO, queryActors, permutation)) {
			match = true;
			break;
		}
	} while(NextFormPermutation(iter));
	FreeFormIterator(iter);
	return match;
}


void DispatchQueryIterate(
	Atom queryTermForm, TypedTuple const * queryActors, index8 permutation[], DispatchIterator * iterator)
{
	ASSERT(IsTermForm(queryTermForm))

	iterator->queryActors = queryActors;
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
			// The permutations of a term are those of its predicate form, the sign
			// contributing none. The relation was found by iterating on the query
			// term form, so its predicate form is the query's.
			if(permutationMatch(
				relation->predicateForm, relation->atomTypes, currentService->parameterIO,
				iterator->queryActors, iterator->permutation)) {
				// Copy the service, as a pointer into the service registry is only
				// valid until the service iterator moves on.
				iterator->service = *currentService;
#ifdef DEBUG
				// The services of one relation are visited in one run, so a second
				// match in the relation just matched is a service that should never
				// have been registered; see ServiceRegistryAdd()
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


bool DispatchQueryAt(
	Atom queryTermForm, TypedTuple const * queryActors, Service * service,
	index8 permutation[], size8 nSkip, bool * hasNextMatch)
{
	// The iterator overwrites its permutation array on every match, so iterate into
	// a scratch array to avoid clobbering the returned permutation.
	size8 termArity = queryActors->nAtoms;
	index8 candidatePermutation[termArity];
	DispatchIterator iterator;
	DispatchQueryIterate(queryTermForm, queryActors, candidatePermutation, &iterator);

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
			CopyMemory(candidatePermutation, permutation, termArity * sizeof(index8));
			// without a hasNextMatch request we can stop at the first match
			if(hasNextMatch == 0)
				break;
		}
	}
	DispatchIteratorEnd(&iterator);

	return match;
}


bool DispatchQuery(Atom queryTermForm, TypedTuple const * queryActors, Service * service, index8 permutation[])
{
	return DispatchQueryAt(queryTermForm, queryActors, service, permutation, 0, 0);
}


bool DispatchQueryFormula(Formula * queryTerm, Service * service, index8 * permutation)
{
	return DispatchQuery(queryTerm->form, queryTerm->actors, service, permutation);
}

