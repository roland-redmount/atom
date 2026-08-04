
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
#include "lang/Form.h"
#include "lang/FormPermutation.h"
#include "lang/Formula.h"
#include "lang/SubstitutionList.h"
#include "lang/Variable.h"
#include "lang/unification.h"

/**
 * Test whether a query tuple matches a service parameters list when permuted
 * according to the given permutation array (0-based indices). The query tuple
 * may contain parameters (used for compilation).
 * Each service input parameter must match a query atom or input parameter of the same type.
 * Each service output parameter must match a query variable or output parameter.
 * Returns true if the tuples match.
 */
static bool signatureQueryTupleMatch(
	byte const * atomTypes, byte const * parameterIO, TypedTuple const * queryActors, index8 const * permutation)
{
	// iterate over query tuple
	for(index8 i = 0; i < queryActors->nAtoms; i++) {
		TypedAtom queryAtom = TypedTupleGetElement(queryActors, permutation[i]);
		byte serviceParameterType = atomTypes[i];
		switch(parameterIO[i]) {
		case PARAMETER_IN:
			if(queryAtom.type == AT_PARAMETER) {
				if(queryAtom.atom.parameter.io != PARAMETER_IN)
					return false;
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
				// if variable is typed, the type must match
				// TODO: typed variables should go away, replaced with AT_PARAMETER
				byte variableType = queryAtom.atom.variable.type;
				if(variableType && (variableType != serviceParameterType))
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
	Atom predicateForm, byte const * atomTypes, byte const * parameterIO,
	TypedTuple const * queryActors, index8 * permutation)
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
	Atom queryTermForm, TypedTuple const * queryActors, ServiceRecord * record,
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
			ServiceRecord const * currentRecord = ServiceIteratorPeekRecord(&serviceIterator);
			if(!permutationMatch(
				predicateForm, relation->atomTypes, currentRecord->parameterIO,
				queryActors, candidatePermutation))
				continue;

			if(match) {
				// An additional match exists beyond the one we will return
				*hasNextMatch = true;
				done = true;
			}
			else if(nMatches++ >= nSkip) {
				match = true;
				// copy the record and its permutation to the caller
				*record = *currentRecord;
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


bool DispatchQuery(Atom queryTermForm, TypedTuple const * queryActors, ServiceRecord * record, index8 permutation[])
{
	return DispatchQueryAt(queryTermForm, queryActors, record, permutation, 0, 0);
}


bool DispatchQueryFormula(Formula * queryTerm, ServiceRecord * record, index8 * permutation)
{
	return DispatchQuery(queryTerm->form, queryTerm->actors, record, permutation);
}

