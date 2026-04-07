
#include "datumtypes/Variable.h"
#include "kernel/dispatch.h"
#include "kernel/list.h"
#include "lang/FormPermutation.h"
#include "lang/Formula.h"
#include "lang/Quote.h"


/**
 * Dispatch a query, 
 * 
 */
Service DispatchQuery(Datum query)
{
	ASSERT(IsFormula(query))
	// TODO:

	// 1) Lookup matching bytecode by form from a service directory.
	
	Datum queryForm = FormulaGetForm(query);
	Service service = RegistryFindService(queryForm);

	// 2) Attempt to match candidate services to the query actors,
	// accounting for permutations and variable types (if any)

	// 3) Execute the service:
	// For tables, run a table iterator. For services, do VM execution.


	/**
	 * In the long run, we want to integrate the REPL into a top-level
	 * VM execution context. That means we need a user input method callable
	 * from the VM that yields a query formula q; this can be a hard-coded
	 * service (user-query q) that blocks until a query is entered.
	 */

	return service;
}


void CallService(Service service, TypedAtom * tuple)
{
	// TODO
	// See VMExecute()

	ASSERT(false);
}


/**
 * Test whether a query tuple matches a signature tuple, in the order given.
 * Non-variable atoms in the query must match signature input arguments,
 * respecting datum type; variables in the query must match output arguments.
 * Writes the the tuple of matched, permuted arguments to *matches (possibly empty)
 * Return true if a match was found.
  */

/*
static bool signatureQueryTupleMatch(Atom queryList, Atom * matches, Signature const * signature)
{
	// both tuples must have same number of atoms
	size32 nAtoms = ListLength(queryList);
	ASSERT(nAtoms <= 255);
	ASSERT(nAtoms == signature->nInputs + signature->nOutputs);
	// iterate over query tuple
	bool match = true;
	for(index8 i = 0, k = 0; i < nAtoms; i++) {
		Atom queryAtom = ListGetElement(queryList, i+1);
		if(i == signature->inputIndex[k]) {
			// input, query atom type must match
			if(signature->inputTypeIds[k] == queryAtom.type) {
				matches[k] = queryAtom;
				k++;
			}
			else {
				match = false;
				break;
			}
		}
		else {
			// output, query atom must be a variable
			if(!IsVariable(queryAtom)) {
				match = false;
				break;
			}
		}
	}
	return match;
}
*/

/*
bool PermutationMatch(Atom formula1, Atom formula2, Atom * args, index8 * perm, Signature const * signature)
{
	// check if forms are identical
	// NOTE: this can be done by dispatch before testing permutations,
	// by doing lookup in a tree indexed by the form datum
	Atom form = FormulaGetForm(formula1);
	if(!SameAtoms(form, FormulaGetForm(formula2)))
		return false;

	// try all permutations of form2
	// Atom actorsList1 = FormulaGetActors(formula1);
	// Datum actorsList2 = FormulaGetActors(formula2);
	FormIterator * iter = CreateFormIterator(form);
	size_t nPerm = 0;
	do {
		nPerm++;
		//printf("nPerm = %llu\n", nPerm);
		GetTuplePermutation(iter, perm);
		// permute tuple
		
		// TODO: this should not create a new DT_LIST but rather
		// use the perm array directly. Unclear what the use case will be,
		// will we perform matches on DT_LIST or in kernel code on Atom * arrays ??
		ASSERT(false);

		// TODO below
		Atom permTuple = invalidAtom; // CreatePermutedTuple(actorsTuple2, perm);
		// check match to permuted tuple

		bool match = signatureQueryTupleMatch(permTuple, args, signature);
		// ReleaseTuple(permTuple);
		if(match) {
			//printf("found match, tried %llu permutations\n", nPerm);
			return true;
		}
	} while(NextFormPermutation(iter));
	// printf("no match, tried %zu permutations\n", nPerm);
	return false;
}
*/

/*
bool SignatureQueryMatch(Signature const * signature, Atom query, Atom * matches, index8 * perm)
{
	return PermutationMatch(signature->formula, query, matches, perm, signature);
}
*/

