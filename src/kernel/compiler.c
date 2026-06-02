#include "kernel/kernel.h"
#include "kernel/expression.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Variable.h"

/*
 * 
 * Dictionary: To match the predicate to a rule, we need to
 * 
 * (1) find a clause form where its form occurs
 * (2) find clauses of that clause form (rules) where the 
 *     corresponding predicate unifies with the query predicate
 * (3) take the remainder of the clause (unified) and repeat from 1
 *     until we reach resolution or there are no more matches
 */


/*
 * A compilation example: say the dictionary contains the rule
 * 
 *   ! + x + y = z | + z - x = y             (1)
 * 
 * and we have a service (+ 1<INT + 2>INT = 3<INT). The query
 * 
 *   + 7 - 4 = d                             (2)
 * 
 * does not match any service, so we need to compile a new service.
 * 
 * To compile a service, we first replace any non-variables in the query
 * with typed, numbered input parameters, so that (2) becomes
 * 
 *   + 1<INT - 2<INT = d                    (3)
 * 
 * This has form (+ - =) which is found in the clause form (! + + = | + - =).
 * Iterating over matching clauses gives the clause (1).
 * Unifying the matched predicate (+ z - x = y) with (+ 1<INT - 2<INT = d)
 * yields the substitution { z -> 1<INT, x -> 2<INT, y -> d }. We then drop the matched
 * predicate, apply this substitution to the remainder of the clause and negate it,
 * which in this case yields
 * 
 *   + 2<INT + d = 1<INT                    (4)
 * 
 * (In general, negating yields a conjunction of predicates.) We then recurse
 * by dispatching the query (4). During dispatch, parameters behave as any atom
 * of the given type, so this query matches the service (+ 1<INT + 2>INT = 3<INT)
 * which points to an EXPRESSION_MACHINE. Unifying the service signature with (3)
 * and renumbering parameters yields the substitution { d -> 3>INT }, and applying
 * this to (3) yields
 * 
 *  + 1<INT - 2<INT = 3>INT                 (5)
 *
 * which becomes the signature of the new service. As we have no more clauses, the
 * found EXPRESSION_MACHINE is the final compilation result, and we create a new
 * service mapping (5) to this expression, which essentally becomes a synonym for
 * the service (+ 1<INT + 2>INT = 3<INT).
 */

/**
 * Compiling a join expression: dictionary contains the rule
 * 
 *   number x plusone y plustwo z <- + x + 1 = y & + y + 1 = z 
 * 
 * or, in CNF
 * 
 *   ! + x + 1 = y | ! + y + 1 = z | number x plusone y plustwo z  (1)
 * 
 * and we have the query (number 3 plusone a plustwo b). We first replace the atom
 * 3 with a parameter 1<INT to give the query
 * 
 *   number 1<INT plusone a plustwo b           (2)
 * 
 * The first round of matching gives the substitution
 * {x -> 1<INT, y -> a, z -> b} and the conjunction
 * 
 *   + 1<INT + 1 = a & + a + 1 = b              (3)
 * 
 * A conjunction will always compile to a JOIN expression. We initialize the join
 * expression with two terms from (3),
 * 
 *   JOIN(+ 1<INT + 1 = a, + a + 1 = b)         (4)
 * 
 * The JOIN expression will compute sequentially from left to right, To find the left and
 * right sub-expressions of the join, we must dispatch the two terms of (4) separaterly.
 * (If we have > 2 terms we can do a series of joins.) Starting (arbitrarily) with
 * the left term, dispatch matches the service (+ 1<INT + 2<INT = 3>INT) which maps
 * to a MACHINE_EXPRESSION. After renumbering we obtain the substitution { a -> 2>INT }
 * that we apply to the _left_ term; for the right term, the output parameter 2 must
 * become an input. So that our JOIN expression is now
 * 
 *   JOIN(+ 1<INT + 1 = 2>INT, + 2<INT + 1 = b)       (5)
 * 
 * When later interpreting this compiled expression, we will evaluate the left sub-expression
 * to obtain values for parameter 2, which will then be copied to input parameter 2 in
 * the right sub-expression.
 * 
 * (If we would have started with the right term, dispatch would not match the service
 * since the variable a does not match the input parameter 2<INT; in this case we
 * would have to postpone this term.)
 * 
 * Continuing with the right term, dispatch again matches (+ 1<INT + 2<INT = 3>INT)
 * yielding the substitution { b -> 3>INT}, and our JOIN expression becomes
 * 
 *   JOIN(+ 1<INT + 1 = 2>INT, + 2<INT + 1 = 3>INT)     (6)
 *
 * Which is now complete as both sub-expressions have been resolved. 
 * Backsubstituting to (2) gives the compiled service signature
 * 
 *   number 1<INT plusone 2>INT plustwo 3>INT           (7)
 * 
 */


 /**
  * In the previous example all variables in the rule were present in the query.
  * On the other hand, with the rule
  * 
  *   number x plustwo z <- + x + 1 = y & + y + 1 = z 
  *
  * and query (number 1 plustwo a) the variable y must be discarded, and then
  * some tuples may become identical. This needs a PROJECT operation in addition
  * to the JOIN,
  * 
  *   PROJECT(JOIN(+ x + 1 = y, + y + 1 = z), {x z})
  * 
  * The PROJECT(expression, variables) operation requires checking for duplicate
  * tuples (unless the variables are known to be a unique key for ther relation).
  * This is problematic since we want the expression to yield one tuple at a time.
  * To enable efficient duplicate removal, the sub-expression must yield tuples in
  * sorted order w.r.t. {x z}. 
  */


 /**
  * Compiling a recursive expression: consider the classic
  * 
  *   integer n factorial f <-
  *     + m + 1 = n & integer m factorial e & * n * e = f
  * 
  * Together with the fact (integer 0 factorial 1) terminating the recursion.
  * (We will need a precondition ? < n > 0: to ensure unique dispatch, but we
  * ignore this for now.) When compiling this expression, the sub-expression
  * (integer m factorial e) will require the service we are currently compiling,
  * so it must be considered by dispatch somehow.
  * 
  * We will compile the query (integer 1<INT factorial f). To construct the first 
  * JOIN expression we will need two resolved terms. The first term (+ m + 1 = n)
  * matches service (+ 1>INT + 2<INT = 3<INT) and we obtain
  * 
  *   JOIN(+ 2>INT + 1 = $1>INT, ...)
  * 
  * the second term is then (integer 2<INT factorial e). We cannot match this to
  * the current service however, since we do not yet know the type of the 
  * 
  */

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

