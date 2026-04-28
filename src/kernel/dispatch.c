
#include "lang/Variable.h"
#include "kernel/dispatch.h"
#include "kernel/list.h"
#include "kernel/Parameter.h"
#include "lang/FormPermutation.h"
#include "lang/Formula.h"
#include "lang/Quote.h"


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


bool DispatchQuery(Atom query, ServiceRecord * record, Tuple * arguments)
{
	ASSERT(IsFormula(query))

	// Test each candidate services using SignatureQueryMatch().
	// There can be only 1 matching service per candidate.

	Atom queryForm = FormulaGetForm(query);
	Atom queryActors = FormulaGetActors(query);
	size8 arity = FormulaArity(query);

	// Iterate over candidate services matching the query form
	RegistryIterator iterator;
	RegistryIterate(queryForm, &iterator);
	index8 permutation[arity];
	bool match = false;
	while(RegistryIteratorHasService(&iterator)) {
		*record = RegistryIteratorGetService(&iterator);
		if(PermutationMatch(queryForm, record->parameters, queryActors, permutation)) {
			match = true;
			break;
		}
		RegistryIteratorNext(&iterator);
	}
	// copy permuted actors list to argument tuple
	for(index8 i = 0; i < arity; i++) {
		TupleSetElement(arguments, i,
			ListGetElement(queryActors, permutation[i] + 1));
	}
	RegistryIteratorEnd(&iterator);
	return match;
}

