#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/expression.h"
#include "kernel/Parameter.h"
#include "kernel/ServiceRegistry.h"
#include "lang/ClauseForm.h"
#include "lang/Formula.h"
#include "lang/SubstitutionList.h"
#include "lang/TermForm.h"
#include "lang/Variable.h"
#include "lang/unification.h"

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
 * Replace atoms in the actors tuple with typed input parameters,
 * leave variables unchanged.
 */
static void atomsToParameters(Tuple const * actors, Tuple * replacedActors)
{
	uint8 parameterNumber = 1;
	for(index8 i = 0; i < actors->nAtoms; i++) {
		TypedAtom a = TupleGetElement(actors, i);
		if(a.type == AT_VARIABLE) {
			TupleSetElement(replacedActors, i, a);
		}
		else {
			Atom parameter = CreateParameter(parameterNumber++, PARAMETER_IN, a.type);
			TupleSetElement(replacedActors, i, CreateTypedAtom(AT_PARAMETER, parameter));
		}
	}
}


/**
 * 
 */
static bool compileService(Atom queryTermForm, Tuple const * queryActors, ServiceRecord * record)
{
	// Generalize atoms in the query to parameters
	Tuple * generalQueryActors = CreateTuple(queryActors->nAtoms);
	atomsToParameters(queryActors, generalQueryActors);
	PrintFormActorsAsFormula(queryTermForm, generalQueryActors);
	PrintChar('\n');

	/**
	 * To find rules (clauses) c that contain a term of the given @term-form,
	 * we first query (multiset c element @term-form multiple _)
	 * and then 
	 */
	RelationBTreeIterator btreeIterator;
	BTree * multisetBTree = RegistryGetCoreBTreeService(FORM_MULTISET_ELEMENT_MULTIPLE);

	Tuple * multisetQueryTuple = CreateTuple(3);
	MultisetSetTuple(
		multisetQueryTuple,
		anonymousVariable,
		CreateTypedAtom(AT_ID, queryTermForm),
		anonymousVariable
	);
	RelationBTreeIterate(multisetBTree, multisetQueryTuple, &btreeIterator);
	bool haveService = false;		
	while(RelationBTreeIteratorNext(&btreeIterator)) {
		// found a multiset where the term form occurs
		TypedAtom clauseForm = RelationBTreeIteratorGetAtom(
			&btreeIterator,
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)
		);
		// ensure the multiset is a clause form
		if(!IsClauseForm(clauseForm.atom))
			continue;
		// the clause must have at least 2 terms
		uint8 clauseNTerms = ClauseFormNTerms(clauseForm.atom);
		ASSERT(clauseNTerms >= 2);

		// NOTE: the term form may occur multiple times in the clause form ?
		size8 multiple = RelationBTreeIteratorGetAtom(
			&btreeIterator,
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)
		).atom;
		ASSERT(multiple == 1)		// for now

		// We may have multiple rules (clauses) with this clause form,
		// resulting in a UNION_EXPRESSION (?)
		DictionaryIterator dictIterator;
		DictionaryIterate(clauseForm.atom, &dictIterator);
		Tuple * matchedTermActors = CreateTuple(queryActors->nAtoms);
		Tuple * substQueryActors = CreateTuple(queryActors->nAtoms);
		while(DictionaryIteratorNext(&dictIterator)) {
			// TODO: handle multiple matching clauses
			ASSERT(!haveService)

			Tuple const * clauseActors = DictionaryIteratorPeekActors(&dictIterator);
			PrintFormActorsAsFormula(clauseForm.atom, clauseActors);
			PrintChar('\n');

			// extract the actor list for the matching term in the clause
			// (assuming multiplicity == 1)
			index8 matchedTermActorsIndex = ClauseGetTermActorsIndex(clauseForm.atom, queryTermForm, 1);
			CopyTuplesOffset(clauseActors, matchedTermActorsIndex, matchedTermActors);

			// unify the matched term with the generalized query term to get a substitution list
			SubstitutionList querySubstitution;
			SubstitutionList clauseSubstitution;
			UnifyTuples(generalQueryActors, matchedTermActors, &querySubstitution, &clauseSubstitution);
			
			PrintSubstitutionList(&querySubstitution);
			PrintChar('\n');
			PrintSubstitutionList(&clauseSubstitution);
			PrintChar('\n');

			// apply the substitutions 
			SubstituteTuple(&querySubstitution, generalQueryActors, substQueryActors);
			PrintFormActorsAsFormula(queryTermForm, substQueryActors);
			PrintChar('\n');
			Tuple * substClauseActors = CreateTuple(clauseActors->nAtoms);
			SubstituteTuple(&clauseSubstitution, clauseActors, substClauseActors);
			PrintFormActorsAsFormula(clauseForm.atom, substClauseActors);
			PrintChar('\n');

			// Find indexes to actors for all terms
			index8 termActorsIndices[clauseNTerms + 1];
			// ClauseGetTermActorsIndices(clauseForm.atom, termActorsIndices);

			/**
			 * Iterate over the remaining terms. 
			 * If we have only 1 term remaining, we compile it directly;
			 * otherwise we must compile a join expression.
			 */
			MultisetIterator termFormIterator;
			MultisetIterate(clauseForm.atom, &termFormIterator);
			size8 nTermsRemaining = clauseNTerms - 1;
			size8 termIndex = 0;
			termActorsIndices[0] = 0;
			while(nTermsRemaining >= 1) {
				ASSERT(MultisetIteratorNext(&termFormIterator))
				ElementMultiple em = MultisetIteratorGetElement(&termFormIterator);
				Atom termForm = em.element.atom;
				size8 termArity = TermFormArity(termForm);
				Atom negatedTermForm = CreateTermForm(
					TermFormGetPredicateForm(termForm),
					!TermFormGetSign(termForm)
				);
				// iterate over all terms of this form
				Tuple * termActors = CreateTuple(termArity);
				for(index8 k = 0; k < em.multiple; k++, termIndex++) {
					termActorsIndices[termIndex + 1] = termActorsIndices[termIndex] + termArity;
					if(termActorsIndices[termIndex] == matchedTermActorsIndex) {
						// skip the matched term
						continue;
					}
					// Extract term actors
					CopyTuplesOffset(substClauseActors, termActorsIndices[termIndex], termActors);
					PrintFormActorsAsFormula(negatedTermForm, termActors);
					PrintChar('\n');
					if(nTermsRemaining == 1) {
						// attempt to locate an service existing service
						index8 permutation[3];
						ServiceRecord termServiceRecord;
						if(DispatchQuery(negatedTermForm, termActors, &termServiceRecord, permutation)) {
							// TODO: we must create a new service record with the generalized query
							// as parameters. For the general case we can collect the expressions
							// of all located services and then create a UNION of them as the final
							// service (if there are > 1 matching services)
							*record = termServiceRecord;
							haveService = true;
						}
					}
					else {
						/**
						 * TODO: 
						 * As long as we have >= 2 terms remaining, we must compile a join expression;
						 * we must then find two terms that can be evaluated with the current
						 * substitution list. Terms that cannot be evaluated must be postponed. 
						 */
						bool termPostponed[clauseNTerms];
						ASSERT(false);
					}
					nTermsRemaining--;
				}
				IFactRelease(negatedTermForm);
				FreeTuple(termActors);
			}
			MultisetIteratorEnd(&termFormIterator);
			FreeTuple(substClauseActors);
			FreeSubstitutionList(&querySubstitution);
			FreeSubstitutionList(&clauseSubstitution);
		}
		DictionaryIteratorEnd(&dictIterator);
		FreeTuple(matchedTermActors);
		FreeTuple(substQueryActors);
	}
	RelationBTreeIteratorEnd(&btreeIterator);
	FreeTuple(multisetQueryTuple);
	FreeTuple(generalQueryActors);
	return haveService;
}


bool CompileService(Atom queryTerm, ServiceRecord * record)
{
	Atom queryTermForm = FormulaGetForm(queryTerm);
	ASSERT(IsTermForm(queryTermForm))
	size8 arity = TermFormArity(queryTermForm);
	Tuple * queryActors = CreateTuple(arity);
	CopyListToTuple(FormulaGetActors(queryTerm), queryActors);

	compileService(queryTermForm, queryActors, record);
	FreeTuple(queryActors);
	return record;
}
