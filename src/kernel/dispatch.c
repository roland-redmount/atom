
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
 * Test whether a query tuple matches a parameters tuple when permuted
 * according to the given permutation araray (0-based indies)
 * Non-variable atoms in the query must match input parameters,
 * respecting atom type; variables in the query must output parameters.
 * Writes the the tuple of matched, permuted arguments to *matches (possibly empty)
 * Return true if a match was found.
  */
static bool signatureQueryTupleMatch(Atom parameterList, Atom queryList, index8 const * permutation)
{
	// both tuples must have same number of atoms
	size32 nAtoms = ListLength(queryList);
	ASSERT(nAtoms <= 255);
	ASSERT(nAtoms == ListLength(parameterList));
	// iterate over query tuple
	for(index8 i = 0; i < nAtoms; i++) {
		TypedAtom queryAtom = ListGetElement(queryList, permutation[i] + 1);
		Atom parameter = ListGetElement(parameterList, i + 1).atom;
		switch(ParameterGetIO(parameter)) {
		case PARAMETER_IN:
			//  query atom type must match
			if(queryAtom.type != ParameterGetType(parameter))
				return false;
			break;
		
		case PARAMETER_OUT:
			// output, query atom must be a variable
			if(queryAtom.type != AT_VARIABLE)
				return false;
			// if variable is typed, the type must match
			byte variableType = VariableGetType(queryAtom.atom);
			if(variableType && (variableType != ParameterGetType(parameter)))
				return false;
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
bool PermutationMatch(Atom form, Atom parametersList, Atom queryList, index8 * permutation)
{
	// iterate over all permutations of the form
	FormIterator * iter = CreateFormIterator(form);
	bool match = false;
	do {
		GetTuplePermutation(iter, permutation);
		if(signatureQueryTupleMatch(parametersList, queryList, permutation)) {
			match = true;
			break;
		}
	} while(NextFormPermutation(iter));
	FreeFormIterator(iter);
	return match;
}


static bool dispatchToService(Atom queryForm, Atom queryActors, ServiceRecord * record, index8 * permutation)
{
	// Iterate over candidate services matching the query form
	RegistryIterator iterator;
	RegistryIterate(queryForm, &iterator);
	bool match = false;
	while(RegistryIteratorNext(&iterator)) {
		ServiceRecord const * currentRecord = RegistryIteratorPeekService(&iterator);
		if(PermutationMatch(queryForm, currentRecord->parameters, queryActors, permutation)) {
			match = true;
			// copy the record to the caller
			*record = *currentRecord;
			break;
		}
	}
	RegistryIteratorEnd(&iterator);
	return match;
}


bool DispatchQuery(Atom query, ServiceRecord * record, index8 * permutation)
{
	ASSERT(IsFormula(query))

	// Test each candidate services using SignatureQueryMatch().
	// There can be only 1 matching service per candidate.

	Atom queryForm = FormulaGetForm(query);
	Atom queryActors = FormulaGetActors(query);
	size8 arity = FormulaArity(query);

	// first try dispatching to an existing service
	bool match = dispatchToService(queryForm, queryActors, record, permutation);
	if(!match) {
		// if no service exists, call the compiler to attempt to compile one

		// TODO: this needs a term, not a predicate
		// match = compileService(query);
		ASSERT(false)
		;
	}
	return match;
}
