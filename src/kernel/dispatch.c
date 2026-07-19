
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/Parameter.h"
#include "kernel/RelationBTree.h"
#include "lang/ClauseForm.h"
#include "lang/Form.h"
#include "lang/FormPermutation.h"
#include "lang/Formula.h"
// #include "lang/Quote.h" 
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
static bool signatureQueryTupleMatch(Atom const * parameters, TypedTuple const * queryActors, index8 const * permutation)
{
	// iterate over query tuple
	for(index8 i = 0; i < queryActors->nAtoms; i++) {
		TypedAtom queryAtom = TypedTupleGetElement(queryActors, permutation[i]);
		byte serviceParameterType = parameters[i].parameter.atomType;
		switch(parameters[i].parameter.io) {
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
static bool permutationMatch(Atom predicateForm, Atom const * parameters, TypedTuple const * queryActors, index8 * permutation)
{
	// iterate over all permutations of the form
	FormIterator * iter = CreateFormIterator(predicateForm);
	bool match = false;
	do {
		GetTuplePermutation(iter, permutation);
		if(signatureQueryTupleMatch(parameters, queryActors, permutation)) {
			match = true;
			break;
		}
	} while(NextFormPermutation(iter));
	FreeFormIterator(iter);
	return match;
}


bool DispatchQuery(Atom queryTermForm, TypedTuple const * queryActors, ServiceRecord * record, index8 * permutation)
{
	ASSERT(IsTermForm(queryTermForm))
	// TODO: currently, the service registry only supports non-negated predicates
	ASSERT(TermFormGetSign(queryTermForm))
	Atom predicateForm = TermFormGetPredicateForm(queryTermForm);
	// Iterate over candidate services matching the predicate form
	RegistryIterator iterator;
	RegistryIterate(predicateForm, &iterator);
	bool match = false;
	while(RegistryIteratorNext(&iterator)) {
		ServiceRecord const * currentRecord = RegistryIteratorPeekService(&iterator);
		if(permutationMatch(predicateForm, currentRecord->parameters, queryActors, permutation)) {
			match = true;
			// copy the record to the caller
			*record = *currentRecord;
			break;
		}
	}
	RegistryIteratorEnd(&iterator);
	return match;
}


bool DispatchQueryFormula(Formula * queryTerm, ServiceRecord * record, index8 * permutation)
{
	return DispatchQuery(queryTerm->form, queryTerm->actors, record, permutation);
}

