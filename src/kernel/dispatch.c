
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
#include "lang/Quote.h"
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
static bool signatureQueryTupleMatch(Atom parameterList, Tuple const * queryActors, index8 const * permutation)
{
	// both tuples must have same number of atoms
	ASSERT(queryActors->nAtoms == ListLength(parameterList));
	// iterate over query tuple
	for(index8 i = 0; i < queryActors->nAtoms; i++) {
		TypedAtom queryAtom = TupleGetElement(queryActors, permutation[i]);
		Atom serviceParameter = ListGetElement(parameterList, i + 1).atom;
		switch(ParameterGetIO(serviceParameter)) {
		case PARAMETER_IN:
			if(queryAtom.type == AT_PARAMETER) {
				return ParameterGetIO(queryAtom.atom) == PARAMETER_IN;
			}
			else {
				// query atom type must match the parameter type
				if(queryAtom.type != ParameterGetType(serviceParameter))
					return false;
				break;
			}
		
		case PARAMETER_OUT:
			if(queryAtom.type == AT_PARAMETER) {
				return ParameterGetIO(queryAtom.atom) == PARAMETER_OUT;
			}
			else {
				if(queryAtom.type != AT_VARIABLE)
					return false;
				// if variable is typed, the type must match
				// TODO: typed variables should go away, replaced with AT_PARAMETER
				byte variableType = VariableGetType(queryAtom.atom);
				if(variableType && (variableType != ParameterGetType(serviceParameter)))
					return false;
			}
			break;
		
		case PARAMETER_IN_OUT:
			// any query atom matches
			;
		}
	}
	return true;
}


/**
 * Enumerate all possible argument permutations for the given form
 * and test each for a match against parametersList.
 * Returns true if a match is found.
 */
bool PermutationMatch(Atom predicateForm, Atom parametersList, Tuple const * queryActors, index8 * permutation)
{
	// iterate over all permutations of the form
	FormIterator * iter = CreateFormIterator(predicateForm);
	bool match = false;
	do {
		GetTuplePermutation(iter, permutation);
		if(signatureQueryTupleMatch(parametersList, queryActors, permutation)) {
			match = true;
			break;
		}
	} while(NextFormPermutation(iter));
	FreeFormIterator(iter);
	return match;
}


bool DispatchQuery(Atom queryTermForm, Tuple const * queryActors, ServiceRecord * record, index8 * permutation)
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
		if(PermutationMatch(predicateForm, currentRecord->parameters, queryActors, permutation)) {
			match = true;
			// copy the record to the caller
			*record = *currentRecord;
			break;
		}
	}
	RegistryIteratorEnd(&iterator);
	return match;
}


bool DispatchQueryFormula(Atom queryTerm, ServiceRecord * record, index8 * permutation)
{
	Atom queryTermForm = FormulaGetForm(queryTerm);
	ASSERT(IsTermForm(queryTermForm))
	Atom queryActorsList = FormulaGetActors(queryTerm);
	size8 termArity = TermFormArity(queryTermForm);
	Tuple * queryActors = CreateTuple(termArity);
	CopyListToTuple(queryActorsList, queryActors);
	bool found = DispatchQuery(queryTermForm, queryActors, record, permutation);
	FreeTuple(queryActors);
	return found;
}

