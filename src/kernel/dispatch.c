
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
	while(RegistryIteratorHasService(&iterator)) {
		*record = RegistryIteratorGetService(&iterator);
		if(PermutationMatch(queryForm, record->parameters, queryActors, permutation)) {
			match = true;
			break;
		}
		RegistryIteratorNext(&iterator);
	}
	RegistryIteratorEnd(&iterator);
	return match;
}

/**
 * To match a predicate to a rule, we need to
 * (1) find a clause form where its form occurs
 * (2) find clauses of that clause form (rules) where the 
 *     corresponding predicate unifies with the query predicate
 * (3) take the remainder of the clause (unified) and repeat from 1
 *     until we reach resolution or there are no more matches
 * 
 * The query (root 3 square s) has form (root square) which occurs
 * in the clause form (! r r s | root square). Iterating over matching
 * clauses, gives the clause
 *
 *   ! * r * r = s | root r square s
 * 
 * Unifying the matching predicate (root r square s) with (root 3 square s)
 * yields the substitution { r-> 3 }. We then negate the remainder of the
 * clause, which in this case yields the single predicate
 * 
 *   * 3 * 3 = s
 * 
 * (In general this becomes a conjunction of predicates.) We then recurse
 * by dispatching the query (* 3 * 3 = s).
 */

/**
 * Compiling the service: for the query (root 3 square s), we will construct
 * a service (root @INT square $) but the output parameter type is not yet known.
 * We replace the constant 3 with a typed variable _r:INT.
 * In the first round of dispatch, we match to the above rule, and unification
 * yields
 * 
 *  * _r:INT * _r:INT = s --> root _r:INT square s
 * 
 * but we still don't know if there is a matching service to call.
 * In the second round of dispatch, (* _r:INT * _r:INT = s) matches the
 * service (* @INT * @INT = $INT), with substition {_r:INT -> @INT, s -> $INT}
 * which yields
 * 
 *  * _r:INT * _r:INT = s --> root _r:INT square s
 * 
 * As we only have a single term, there is no join iteration, and we can generate
 * 
 * root @INT square $INT
 * 1  COPY   <* * = > #1
 * 2  COPY   @1 #1.@1
 * 3  COPY   @1 #1.@2
 * 4  CALL   #1
 * 5  JUMPIF 7
 * 6  END
 * 7  YIELD
 * 8  JMP 4
 * 
 * where on line 1 we insert the found service, and on lines 2--3 we copy parameters
 * according to the found substitution. Lines 4--8 is the same for all services.
 * (If we know in advance that a service returns exactly once, we can simplify.)
 * 
 * Note that we might follow several rules before we find a service to call.
 */
static bool compileService(Atom queryTerm)
{
	Atom queryTermForm = FormulaGetForm(queryTerm);
	ASSERT(IsTermForm(queryTermForm))
	Tuple * queryTermActors = CreateTuple(2);
	CopyListToTuple(FormulaGetActors(queryTerm), queryTermActors);
	/**
	 * To search find rules (clauses) c that contain a given @term-form,
	 * we first query (clause-form c) & (multiset c element @term_form multiple _)
	 */
	RelationBTreeIterator btreeIterator;
	BTree * multisetBTree = RegistryGetCoreTable(FORM_MULTISET_ELEMENT_MULTIPLE);

	Tuple * multisetQueryTuple = CreateTuple(3);
	MultisetSetTuple(
		multisetQueryTuple,
		anonymousVariable,
		(TypedAtom) {.type = AT_ID, .atom = queryTermForm},
		anonymousVariable
	);
	RelationBTreeIterate(multisetBTree, multisetQueryTuple, &btreeIterator);
	while(RelationBTreeIteratorHasTuple(&btreeIterator)) {
		// a clause form where the term form occurs
		// NOTE: the term form may occur more than once in the clause form
		TypedAtom clauseForm = RelationBTreeIteratorGetAtom(
			&btreeIterator,
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)
		);
		// number of times the term form occurs in this clause form
		size8 multiple = RelationBTreeIteratorGetAtom(
			&btreeIterator,
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)
		).atom;
		if(clauseForm.type == AT_ID && IsClauseForm(clauseForm.atom)) {
			PrintClauseForm(clauseForm.atom);
			PrintChar('\n');

			// we may have multiple rules with this clause form
			DictionaryIterator dictIterator;
			DictionaryIterate(clauseForm.atom, &dictIterator);
			while(DictionaryIteratorHasRecord(&dictIterator)) {
				Tuple const * clauseActors = DictionaryIteratorPeekActors(&dictIterator);
				PrintTuple(clauseActors);
				PrintChar('\n');

				// extract the actor list for a matching term in the clause
				// here we take the first occurence; when there are more than one occurence,
				// each gives rise to a separate unification
				Tuple * clauseTermActors = CreateTuple(2);
				ClauseGetTermActors(clauseForm.atom, clauseActors, queryTermForm, clauseTermActors, 1);
				
				// unify the matched term with the query term
				SubstitutionList querySubstitution;
				SubstitutionList clauseSubstitution;
				UnifyTuples(queryTermActors, clauseTermActors, &querySubstitution, &clauseSubstitution);
				
				// apply the substitution to the clause

				// drop the matched term

				// recursively dispatch on the negation of the remaining clause
				// (a conjunction)

				DictionaryIteratorNext(&dictIterator);
			}
			DictionaryIteratorEnd(&dictIterator);
		}
		RelationBTreeIteratorNext(&btreeIterator);
	}
	RelationBTreeIteratorEnd(&btreeIterator);
	FreeTuple(multisetQueryTuple);

	// TODO
	return false;
}


bool DispatchQuery(Atom query, ServiceRecord * record, Tuple * arguments)
{
	ASSERT(IsFormula(query))

	// Test each candidate services using SignatureQueryMatch().
	// There can be only 1 matching service per candidate.

	Atom queryForm = FormulaGetForm(query);
	Atom queryActors = FormulaGetActors(query);
	size8 arity = FormulaArity(query);

	index8 permutation[arity];
	// first try dispatching to an existing service
	bool match = dispatchToService(queryForm, queryActors, record, permutation);
	if(!match) {
		// if no service exists, attempt to compile one

		// TODO: this needs a term, not a predicate
		match = compileService(query);
	}

	// copy permuted actors list to argument tuple
	for(index8 i = 0; i < arity; i++) {
		TupleSetElement(arguments, i,
			ListGetElement(queryActors, permutation[i] + 1));
	}
	return match;
}
