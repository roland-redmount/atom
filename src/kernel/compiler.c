#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/service.h"
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
 * service record mapping (5) to this service, which essentally becomes a synonym for
 * (+ 1<INT + 2>INT = 3<INT).
 */

/**
 * Compiling a join service: dictionary contains the rule
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
 * A conjunction will always compile to a JOIN service. We initialize the join
 * service with two terms from (3),
 * 
 *   JOIN(+ 1<INT + 1 = a, + a + 1 = b)         (4)
 * 
 * The JOIN service will compute sequentially from left to right, To find the left and
 * right child services of the join, we must dispatch the two terms of (4) separaterly.
 * (If we have > 2 terms we can do a series of joins.) Starting (arbitrarily) with
 * the left term, dispatch matches the service (+ 1<INT + 2<INT = 3>INT) which maps
 * to a MACHINE_EXPRESSION. After renumbering we obtain the substitution { a -> 2>INT }
 * that we apply to the _left_ term; for the right term, the output parameter 2 must
 * become an input. So that our JOIN service is now
 * 
 *   JOIN(+ 1<INT + 1 = 2>INT, + 2<INT + 1 = b)       (5)
 * 
 * When later executing this compiled service, we will evaluate the left child service
 * to obtain values for parameter 2, which will then be copied to input parameter 2 in
 * the right child service.
 * 
 * (If we would have started with the right term, dispatch would not match the service
 * since the variable a does not match the input parameter 2<INT; in this case we
 * would have to postpone this term.)
 * 
 * Continuing with the right term, dispatch again matches (+ 1<INT + 2<INT = 3>INT)
 * yielding the substitution { b -> 3>INT}, and our JOIN service becomes
 * 
 *   JOIN(+ 1<INT + 1 = 2>INT, + 2<INT + 1 = 3>INT)     (6)
 *
 * Which is now complete as both child services have been resolved. 
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
  * The PROJECT(service, variables) operation requires checking for duplicate
  * tuples (unless the variables are known to be a unique key for ther relation).
  * This is problematic since we want the service to yield one tuple at a time.
  * To enable efficient duplicate removal, the child service must yield tuples in
  * sorted order w.r.t. {x z}. 
  */


 /**
  * Compiling a recursive service: consider the classic
  * 
  *   integer n factorial f <-
  *     + m + 1 = n & integer m factorial e & * n * e = f
  * 
  * Together with the fact (integer 0 factorial 1) terminating the recursion.
  * (We will need a precondition ? < n > 0: to ensure unique dispatch, but we
  * ignore this for now.) When compiling this service, the child service
  * (integer m factorial e) will require the service we are currently compiling,
  * so it must be considered by dispatch somehow.
  * 
  * We will compile the query (integer 1<INT factorial f). To construct the first 
  * JOIN service we will need two resolved terms. The first term (+ m + 1 = n)
  * matches service (+ 1>INT + 2<INT = 3<INT) and we obtain
  * 
  *   JOIN(+ 2>INT + 1 = $1>INT, ...)
  * 
  * the second term is then (integer 2<INT factorial e). We cannot match this to
  * the current service however, since we do not yet know the type of the 
  * 
  */


/**
 * Replace non-variable atoms in the actors tuple with typed input parameters,
 * and variables with untyped output parameters. The output parameter types
 * must be discovered by matching against services. The genererated parameter
 * numbers are always equan to the tuple index + 1.
 */
static void atomsToParameters(Tuple const * actors, Tuple * replacedActors)
{
	for(index8 i = 0; i < actors->nAtoms; i++) {
		TypedAtom typedAtom = TupleGetElement(actors, i);
		if(typedAtom.type == AT_VARIABLE) {
			Atom parameter = CreateParameter(i + 1, PARAMETER_OUT, 0);
			TupleSetElement(replacedActors, i, CreateTypedAtom(AT_PARAMETER,parameter));
		}
		else {
			Atom parameter = CreateParameter(i + 1, PARAMETER_IN, typedAtom.type);
			TupleSetElement(replacedActors, i, CreateTypedAtom(AT_PARAMETER, parameter));
		}
	}
}


static bool unifyAndSubstitute(
	Tuple const * tuple1, Tuple const * tuple2, Tuple * substTuple1, Tuple * substTuple2)
{
	// unify the matched term with the generalized query term to get a substitution list
	Substitution substitution1;
	Substitution substitution2;
	bool success = UnifyTuples(tuple1, tuple2, &substitution1, &substitution2);
	// PrintSubstitutionList(&substitution1);
	// PrintChar('\n');
	// PrintSubstitutionList(&substitution2);
	// PrintChar('\n');

	// apply the substitutions in-place
	SubstituteTuple(&substitution1, substTuple1, substTuple1);
	SubstituteTuple(&substitution2, substTuple2, substTuple2);

	FreeSubstitution(&substitution1);
	FreeSubstitution(&substitution2);
	return success;
}


/**
 * Compile a JOIN service from the conjuction obtained by negating
 * the given clause. We iterate over all negated terms until we find
 * a terms that resolve to a known service; we then create a JOIN
 * between this service and the service obtained by recursively
 * compilin the remaining terms. If there is only 1 term to consider,
 * we emit its service directly without a JOIN, terminating recursion.
 * 
 */

static bool compileConjunctionRecursive(
	Atom clauseForm, Tuple * clauseActors, bool * termExcluded, uint8 nTermsExcluded,
	index8 const * termActorsIndices, Service * service)
{
	PrintCString("** compileConjunctionRecursive()\n");
				
	uint8 clauseNTerms = ClauseFormNTerms(clauseForm);
	bool success = false;

	// Iterate over term forms
	MultisetIterator termFormIterator;
	MultisetIterate(clauseForm, &termFormIterator);
	size8 termIndex = 0;
	while(MultisetIteratorNext(&termFormIterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&termFormIterator);
		Atom termForm = em.element.atom;
		size8 termArity = TermFormArity(termForm);
		// negate the term form
		Atom negatedTermForm = CreateTermForm(
			TermFormGetPredicateForm(termForm),
			!TermFormGetSign(termForm)
		);
		// iterate over all terms (multiples) of this form
		Tuple * termActors = CreateTuple(termArity);
		for(index8 k = 0; k < em.multiple; k++, termIndex++) {
			if(termExcluded[termIndex])
				continue;
			// Extract term actors
			CopyTuplesOffset(clauseActors, termActorsIndices[termIndex], termActors);
			PrintCString("Term: ");
			PrintFormActorsAsFormula(negatedTermForm, termActors);
			PrintChar('\n');

			// attempt to locate an service existing service
			index8 permutation[termArity];
			ServiceRecord termServiceRecord;
			if(DispatchQuery(negatedTermForm, termActors, &termServiceRecord, permutation)) {
				PrintCString("termServiceRecord: ");
				PrintServiceRecord(&termServiceRecord);
				PrintChar('\n');
				// Update clause parameters types from service parameters
				// NOTE: this essentially means to construct a substitution list form the match
				// and apply it to the full clause.
				Tuple * serviceParameters = CreateTuple(termArity);
				CopyListToTuple(termServiceRecord.parameters, serviceParameters);

				Substitution clauseSubst;
				SetupSubstitution(&clauseSubst, termActors->nAtoms);
				
				for(index8 i = 0; i < termActors->nAtoms; i++) {
					TypedAtom queryParameter = TupleGetElement(termActors, i);
					if(queryParameter.type != AT_PARAMETER)
						continue;
					TypedAtom serviceParameter = TupleGetElement(serviceParameters, i);
					ASSERT(serviceParameter.type == AT_PARAMETER)
					byte queryParameterType = ParameterGetType(queryParameter.atom);
					if(!queryParameterType) {
						ASSERT(ParameterGetIO(queryParameter.atom) == PARAMETER_OUT)
						// update untyped output parameter
						Atom updatedParameter = CreateParameter(
							ParameterGetNumber(queryParameter.atom),
							PARAMETER_OUT,
							ParameterGetType(serviceParameter.atom)
						);
						SubstitutionSetValue(&clauseSubst, queryParameter, 
							CreateTypedAtom(AT_PARAMETER, updatedParameter));
					}
				}
				PrintCString("Substitution: ");
				PrintSubstitution(&clauseSubst);
				PrintChar('\n');

				SubstituteTuple(&clauseSubst, clauseActors, clauseActors);
				FreeSubstitution(&clauseSubst);
				FreeTuple(serviceParameters);

				PrintCString("Updated clause: ");
				PrintFormActorsAsFormula(clauseForm, clauseActors);
				PrintChar('\n');

				termExcluded[termIndex] = true;

				if(nTermsExcluded + 1 == clauseNTerms) {
					// No more terms to consider, return the service
					*service = termServiceRecord.service;

					success = true;
				}
				else {
					// Recurse on remaining terms
					Service rightExpression;
					if(compileConjunctionRecursive(
						clauseForm, clauseActors, termExcluded, nTermsExcluded + 1,
						termActorsIndices, &rightExpression)
					) {
						// TODO: how to compute argument mapping?
						// The left term is an identity map against the term.
						// The term actors may derive from the top-level query arguments,
						// but can also be constants or variables determined by evaluating
						// other terms. 
						size8 rightNArguments = rightExpression.dimensions.nArguments;
						size8 nArguments = termArity + rightNArguments;
						index8 argumentMap[nArguments];
						SetupJoinService(
							service, nArguments, argumentMap,
							&termServiceRecord.service, &rightExpression);
						success = true;
					}
				}
			}
			// else we re-try term after this round is completed
		}
		IFactRelease(negatedTermForm);
		FreeTuple(termActors);
	}

	MultisetIteratorEnd(&termFormIterator);
	return success;
}


static bool compileConjunction(
	Atom clauseForm, Tuple * clauseActors, index8 matchedTermIndex, Service * service)
{
	uint8 clauseNTerms = ClauseFormNTerms(clauseForm);
	index8 termActorsIndices[clauseNTerms + 1];
	ClauseGetTermActorsIndices(clauseForm, termActorsIndices);
	termActorsIndices[0] = 0;
	bool termExcluded[clauseNTerms];
	for(index8 i = 0; i < clauseNTerms; i++)
		termExcluded[i] = (i == matchedTermIndex);

	return compileConjunctionRecursive(
		clauseForm, clauseActors, termExcluded, 1, termActorsIndices, service);
}


/**
 * Attempt to compile a service with the given form and actors (parameters).
 * If compilation succeeds, write the resulting Expression to the given pointer.
 */
static bool compileService(Atom queryTermForm, Tuple * queryActors, Service * service)
{
	size8 termArity = TermFormArity(queryTermForm);
	/**
	 * To find rules (clauses) c that contains a matching term form,
	 * we query (multiset c element @term-form multiple _),
	 * If multiple rules match, we must generate a UNION over
	 * the resulting services
	 */
	Service joinService;	// for now we will return a single service

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
		// Found a multiset where the term form occurs
		TypedAtom clauseForm = RelationBTreeIteratorGetAtom(
			&btreeIterator,
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)
		);
		// Ensure the multiset is a clause form
		if(!IsClauseForm(clauseForm.atom))
			continue;
		// The clause must have at least 2 terms
		uint8 clauseNTerms = ClauseFormNTerms(clauseForm.atom);
		ASSERT(clauseNTerms >= 2);

		size8 multiple = RelationBTreeIteratorGetAtom(
			&btreeIterator,
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)
		).atom;

		// Iterate over all rules (clauses) with this clause form.
		DictionaryIterator dictIterator;
		DictionaryIterate(clauseForm.atom, &dictIterator);
		Tuple * matchedTermActors = CreateTuple(queryActors->nAtoms);
		Tuple * substClauseActors = CreateTuple(ClauseArity(clauseForm.atom));
		while(DictionaryIteratorNext(&dictIterator)) {
			// TODO: we need UNION to handle multiple matching clauses
			ASSERT(!haveService)

			Tuple const * clauseActors = DictionaryIteratorPeekActors(&dictIterator);
			PrintCString("Matched rule: ");
			PrintFormActorsAsFormula(clauseForm.atom, clauseActors);
			PrintChar('\n');

			// Iterate over all occurences of the query term in the matched clause
			// and find one that unifies, if any
			bool unified = false;
			index8 matchedTermActorsIndex = ClauseGetTermActorsIndex(clauseForm.atom, queryTermForm, 1);
			for(index8 k = 1; k <= multiple; k++, matchedTermActorsIndex += termArity) {
				// extract actors for the matching term in the clause
				CopyTuplesOffset(clauseActors, matchedTermActorsIndex, matchedTermActors);
				// unify the query with the matched term
				Substitution querySubst;
				Substitution termSubst;
				if(UnifyTuples(queryActors, matchedTermActors, &querySubst, &termSubst)) {
					ASSERT(!unified)	// only one term may unify (?)
					unified = true;
					SubstituteTuple(&termSubst, clauseActors, substClauseActors);
				}
				FreeSubstitution(&querySubst);
				FreeSubstitution(&termSubst);

				// TODO: we must determine the unique arguments
				// from the unification = components in the unification graph.
			}
			if(!unified)
				continue;
			PrintCString("Unified rule: ");
			PrintFormActorsAsFormula(clauseForm.atom, substClauseActors);
			PrintChar('\n');

			index8 matchedTermIndex = ClauseGetTermIndex(clauseForm.atom, queryTermForm, 1);
			haveService = compileConjunction(clauseForm.atom, substClauseActors, matchedTermIndex, &joinService);
			if(haveService) {
				// Update the query actors from the clause actors
				// to recover unified parameters
				// NOTE: when multiple services are matched, the resulting parameter tuple
				// must be identical across all services (same parameter types)
				CopyTuplesOffset(substClauseActors, matchedTermActorsIndex, queryActors);
			}
		}
		DictionaryIteratorEnd(&dictIterator);
		FreeTuple(substClauseActors);
		FreeTuple(matchedTermActors);
	}
	RelationBTreeIteratorEnd(&btreeIterator);
	FreeTuple(multisetQueryTuple);

	*service = joinService;
	return haveService;
}


bool CompileService(Atom queryTerm, ServiceRecord * record)
{
	Atom queryTermForm = FormulaGetForm(queryTerm);
	ASSERT(IsTermForm(queryTermForm))
	size8 arity = TermFormArity(queryTermForm);
	Tuple * queryActors = CreateTuple(arity);
	CopyListToTuple(FormulaGetActors(queryTerm), queryActors);

	// Generalize atoms in the query to parameters
	// NOTE: the compilation process will determine the type
	// of any output parameters. 
	Tuple * serviceParameters = CreateTuple(queryActors->nAtoms);
	atomsToParameters(queryActors, serviceParameters);
	FreeTuple(queryActors);

	PrintCString("Generalized query: ");
	PrintFormActorsAsFormula(queryTermForm, serviceParameters);
	PrintChar('\n');

	bool success = compileService(queryTermForm, serviceParameters, &record->service);
	if(success) {
		record->parameters = CreateListFromTuple(serviceParameters);
		record->form = queryTermForm;
		RegistryAddService(record);
		IFactRelease(record->parameters);
	}
	FreeTuple(serviceParameters);
	return success;
}
