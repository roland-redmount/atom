
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


bool DispatchQueryAt(
	Atom queryTermForm, TypedTuple const * queryActors, Service * service,
	index8 permutation[], size8 nSkip, bool * hasNextMatch)
{
	ASSERT(IsTermForm(queryTermForm))
	// TODO: currently, the service registry only supports non-negated predicates
	ASSERT(TermFormGetSign(queryTermForm))
	Atom predicateForm = TermFormGetPredicateForm(queryTermForm);

	bool match = false;
	bool done = false;
	// Number of matches seen so far, whether skipped or returned
	index8 nMatches = 0;
	if(hasNextMatch)
		*hasNextMatch = false;
	// permutationMatch() overwrites its permutation argument on every match,
	// so probe into a scratch array to avoid clobbering the returned one.
	size8 termArity = queryActors->nAtoms;
	index8 candidatePermutation[termArity];

	// Iterate over relations matching the form.
	// NOTE: this iteration order must be deterministic, as the compiler
	// identifies a choice point by the position of its match in this sequence.
	RelationIterator relationIterator;
	RelationRegistryIterate(predicateForm, &relationIterator);
	while(!done && RelationIteratorNext(&relationIterator)) {
		RelationTable const * relation = RelationIteratorGet(&relationIterator);

		// Iterate over candidate services for the relation table
		// TODO: this is inefficient, would be better to test once if the relation table
		// atom types are compatible with the query, and only then iterate over services.
		ServiceIterator serviceIterator;
		ServiceRegistryIterate(relation, &serviceIterator);
		while(!done && ServiceIteratorNext(&serviceIterator)) {
			Service const * currentService = ServiceIteratorPeekService(&serviceIterator);
			if(!permutationMatch(
				predicateForm, relation->atomTypes, currentService->parameterIO,
				queryActors, candidatePermutation))
				continue;

			if(match) {
				// An additional match exists beyond the one we will return
				*hasNextMatch = true;
				done = true;
			}
			else if(nMatches++ >= nSkip) {
				match = true;
				// copy the service struct and its permutation to the caller
				*service = *currentService;
				CopyMemory(candidatePermutation, permutation, termArity * sizeof(index8));
				// without a hasMore request we can stop at the first match
				done = (hasNextMatch == 0);
			}
		}
		ServiceIteratorEnd(&serviceIterator);
	}
	RelationIteratorEnd(&relationIterator);

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

