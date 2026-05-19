#include "kernel/kernel.h"
#include "kernel/service.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Variable.h"


static ServiceRecord compileCallExpression(Atom queryForm, Tuple * parameters)
{
	// Iterate over candidate services matching the query form
	RegistryIterator iterator;
	RegistryIterate(queryForm, &iterator);
	bool match = false;
	ServiceRecord record;
	// TODO: rewrite the ServiceRegistry to store service indexed by
	// term forms, not predicate forms; and matching should take with a parameter tuple,
	// not a query tuple. The translation query tuple -> parameters happens in the compiler.
/*
	while(RegistryIteratorHasService(&iterator)) {
		record = RegistryIteratorGetService(&iterator);
		// TODO: this should take a Tuple, not a list 
		if(PermutationMatch(queryForm, record.parameters, parameters, permutation)) {
			match = true;
			break;
		}
		RegistryIteratorNext(&iterator);
	}
	RegistryIteratorEnd(&iterator);

	Expression expression = {0};
	if(match) {
		expression.type = CALL_EXPRESSION;
		expression.fields.record = record;

	}
*/
	ASSERT(false)
	return (ServiceRecord) {0};
}


static ServiceRecord compileJoinExpression(Atom const * terms)
{
	ASSERT(false);
	return (ServiceRecord) {0};
}


/**
 * Compile a query (a term) to create a new service.
 */
static ServiceRecord compileService(Atom queryTerm)
{
	Atom queryTermForm = FormulaGetForm(queryTerm);
	ASSERT(IsTermForm(queryTermForm))
	size8 arity = TermFormArity(queryTermForm);
	Tuple * queryTermActors = CreateTuple(arity);
	CopyListToTuple(FormulaGetActors(queryTerm), queryTermActors);
	/**
	 * TODO: the query actors can be generalized to parameters as:
	 *   atom -> in parameter, of same type
	 *   variable -> out parameter (any type)
	 */
	Tuple * parameters = CreateTuple(arity);
	ActorsToParameters(queryTermActors, parameters);

	// first try locating an existing service
/*
	ServiceRecord service = {0};
	service = compileCallExpression(queryTermForm, queryTermActors);
	if(service.type == SERVICE_MACHINE)
		return expression;
*/

	/**
	 * To find rules (clauses) c that contain a given @term-form,
	 * we first query (clause-form c) & (multiset c element @term_form multiple _)
	 */
	RelationBTreeIterator btreeIterator;
	BTree * multisetBTree = RegistryGetCoreBTreeService(FORM_MULTISET_ELEMENT_MULTIPLE);

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
		TypedAtom clauseForm = RelationBTreeIteratorGetAtom(
			&btreeIterator,
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)
		);
		if(!IsClauseForm(clauseForm.atom))
			continue;

		// NOTE: the term form may occur multiple times in the clause form ?
		size8 multiple = RelationBTreeIteratorGetAtom(
			&btreeIterator,
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)
		).atom;
		ASSERT(multiple == 1)		// for now

		if(clauseForm.type == AT_ID && IsClauseForm(clauseForm.atom)) {
			PrintClauseForm(clauseForm.atom);
			PrintChar('\n');

			// We may have multiple rules (clauses) with this clause form,
			// resulting in a UNION_EXPRESSION
			DictionaryIterator dictIterator;
			DictionaryIterate(clauseForm.atom, &dictIterator);
			while(DictionaryIteratorHasRecord(&dictIterator)) {
				Tuple const * clauseActors = DictionaryIteratorPeekActors(&dictIterator);
				PrintTuple(clauseActors);
				PrintChar('\n');

				// extract the actor list for the matching term in the clause
				// (assuming multiplicity == 1)
				Tuple * clauseTermActors = CreateTuple(2);
				ClauseGetTermActors(clauseForm.atom, clauseActors, queryTermForm, clauseTermActors, 1);
				
				// unify the matched term with the query term to get a substitution list
				SubstitutionList querySubstitution;
				SubstitutionList clauseSubstitution;
				UnifyTuples(queryTermActors, clauseTermActors, &querySubstitution, &clauseSubstitution);

				// apply the substitution to the clause
				Tuple * substitutedClause;
				// ...

				// drop the matched term from the clauseForm
				size8 nTerms = ClauseNTermsTotal(clauseForm.atom) - 1;
				Atom conjunctionTerms[nTerms];
				// copy the non-matched terms ...

				// 
				Expression expr = compileJoinExpression(conjunctionTerms);

				DictionaryIteratorNext(&dictIterator);
			}
			DictionaryIteratorEnd(&dictIterator);
		}
		RelationBTreeIteratorNext(&btreeIterator);	// TODO: this should change now
	}
	RelationBTreeIteratorEnd(&btreeIterator);
	FreeTuple(multisetQueryTuple);

	// TODO
	return expression;
}

