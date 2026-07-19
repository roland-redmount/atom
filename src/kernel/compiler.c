#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/service.h"
#include "kernel/Parameter.h"
#include "kernel/RelationTable.h"
#include "kernel/ServiceRegistry.h"
#include "lang/ClauseForm.h"
#include "lang/Formula.h"
#include "lang/SubstitutionList.h"
#include "lang/TermForm.h"
#include "lang/Variable.h"
#include "lang/unification.h"
#include "memory/allocator.h"


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
static void atomsToParameters(TypedTuple const * actors, TypedTuple * replacedActors)
{
	for(index8 i = 0; i < actors->nAtoms; i++) {
		TypedAtom typedAtom = TypedTupleGetElement(actors, i);
		if(typedAtom.type == AT_VARIABLE) {
			Atom parameter = {
				.parameter = {.number = i + 1, .io = PARAMETER_OUT, .atomType = 0}
			};
			TypedTupleSetElement(replacedActors, i, CreateTypedAtom(AT_PARAMETER, parameter));
		}
		else {
			Atom parameter = {
				.parameter = {.number = i + 1, .io = PARAMETER_IN, .atomType = typedAtom.type}
			};
			TypedTupleSetElement(replacedActors, i, CreateTypedAtom(AT_PARAMETER, parameter));
		}
	}
}


/**
 * A term compiles to a PERMUTE service. The numbering of Parameters in the
 * term actors defines the caller's parameter order; any non-parameter atoms
 * are stored in a "constants" tuple. If variables are present, a DEDUPLICATE
 * service is generated to ensure the resulting tuples are unique.
 * The serviceParameters tuple is set to the matched service parameters,
 * permuted to match the term actors order.
 */
static Service * compileTerm(
	Atom termForm, TypedTuple const * termActors, size8 nArguments, TypedTuple * serviceParameters)
{
	// attempt to locate an service existing service
	size8 termArity = termActors->nAtoms;
	index8 permutation[termArity];
	ServiceRecord termServiceRecord;
	if(!DispatchQuery(termForm, termActors, &termServiceRecord, permutation))
		return 0;

	// Compute argument map: 1-based index for each service parameter into the term actors,
	// respecting the argument permutation obtained from DispatchQuery() above
	index8 argumentMap[termArity];
	size8 nConstants = 0;
	for(index8 i = 0; i < termArity; i++) {
		TypedAtom actor = TypedTupleGetElement(termActors, permutation[i]);
		if(actor.type == AT_PARAMETER) {
			argumentMap[i] = actor.atom.parameter.number;
		}
		else {
			argumentMap[i] = 0;
			nConstants++;
		}
		TypedTupleSetElement(serviceParameters, permutation[i],
			CreateTypedAtom(AT_PARAMETER, termServiceRecord.parameters[i + 1]));
	}
	bool deduplicate = false;
	Service * service;
	if(nConstants > 0) {
		Atom constants[nConstants];
		for(index8 i = 0, k = 0; i < termActors->nAtoms; i++) {
			TypedAtom actor = TypedTupleGetElement(termActors, permutation[i]);
			if(actor.type != AT_PARAMETER) {
				constants[k++] = actor.atom;
				// TODO: we should only deduplicate if the "constant" is a variable?
				deduplicate = true;
			}
		}
		service = CreatePermuteService(
			nArguments, constants, argumentMap, termServiceRecord.service);
	}
	else {
		service = CreatePermuteService(
			nArguments, 0, argumentMap, termServiceRecord.service);
	}

	// TODO: if we have an identity argument map, we can just return the child service

	// Wrap in a deduplicate service if needed
	if(deduplicate) {
		Service * deduplicateService = CreateDeduplicateService(service);
		ReleaseService(service);
		service = deduplicateService;
	}
	return service;
}


/**
 * Compile a JOIN service from the conjuction obtained by negating
 * the given clause. We iterate over all negated terms until we find
 * a term that resolve to a known service; we then create a JOIN
 * between this service and the service obtained by recursively
 * compiling the remaining terms. If there is only 1 term to consider,
 * we emit its service directly without a JOIN, terminating recursion.
 */
static Service * compileConjunctionRecursive(
	Atom clauseForm, TypedTuple * clauseActors, index8 matchedTermIndex, size8 nArguments,
	bool * termExcluded, uint8 nTermsExcluded, index8 const * termActorsIndices)
{
	uint8 clauseNTerms = ClauseFormNTerms(clauseForm);
	Service * service = 0;

	// Find a term that can be compiled.
	// First iterate over term forms in the clause form
	MultisetIterator termFormIterator;
	MultisetIterate(clauseForm, &termFormIterator);
	size8 termIndex = 0;
	while(!service && nTermsExcluded < clauseNTerms && MultisetIteratorNext(&termFormIterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&termFormIterator);
		if(termIndex == matchedTermIndex) {
			termIndex += em.multiple;
			continue;
		}
		Atom termForm = em.element.atom;
		size8 termArity = TermFormArity(termForm);
		// negate the term form
		Atom negatedTermForm = CreateTermForm(
			TermFormGetPredicateForm(termForm),
			!TermFormGetSign(termForm)
		);
		// iterate over all terms (multiples) of this form
		TypedTuple * termActors = CreateTypedTuple(termArity);
		TypedTuple * serviceParameters = CreateTypedTuple(termArity);
		for(index8 m = 0; m < em.multiple; m++, termIndex++) {
			if(termExcluded[termIndex])
				continue;
			// Extract term actors
			TypedTupleCopyAt(clauseActors, termActorsIndices[termIndex], termActors);
			PrintCString("Term: ");
			PrintFormActorsAsFormula(negatedTermForm, termActors);
			PrintChar('\n');

			service = compileTerm(negatedTermForm, termActors, nArguments, serviceParameters);
			PrintCString("serviceParameter = ");
			TypedTuplePrint(serviceParameters);
			PrintChar('\n');
			if(service) {
				termExcluded[termIndex] = true;
				nTermsExcluded++;
				// Update parameter types for output parameter in the query term,
				// and for all other terms flip matched outputs to inputs.
				for(index8 i = 0; i < termArity; i++) {
					TypedAtom termActor = TypedTupleGetElement(termActors, i);
					if(termActor.type == AT_PARAMETER) {
						if(!termActor.atom.parameter.atomType) {
							// Corresponding service parameter must be a typed output
							TypedAtom serviceParameter = TypedTupleGetElement(serviceParameters, i);
							ASSERT(serviceParameter.type == AT_PARAMETER)
							ASSERT(serviceParameter.atom.parameter.io == PARAMETER_OUT)
							byte parameterType = serviceParameter.atom.parameter.atomType;
							ASSERT(parameterType)
							// Updated parameters
							index8 queryTermParameterNr = termActor.atom.parameter.number;
							Atom inputParameter = {
								.parameter = {
									.number = queryTermParameterNr,
									.io = PARAMETER_IN,
									.atomType = parameterType
								}
							};
							Atom outputParameter = {
								.parameter = {
									.number = queryTermParameterNr,
									.io = PARAMETER_OUT,
									.atomType = parameterType
								}
							};
							// Replace untyped output parameter in query matched term
							index8 queryParameterIndex = termActorsIndices[matchedTermIndex] + queryTermParameterNr - 1;
							TypedTupleSetAtom(clauseActors, queryParameterIndex, outputParameter);
							// Replace untyped parameter in compiled term
							// NOTE: not necessary, this term is not used for anything at this point
							TypedTupleSetAtom(
								clauseActors, termActorsIndices[termIndex] + i, outputParameter);
							
							// Replace matching parameters in all other non-excluded terms
							for(index8 i = 0; i < clauseNTerms; i++) {
								if(termExcluded[i])
									continue;
								for(index8 j = termActorsIndices[i]; j < termActorsIndices[i + 1]; j++) {
									TypedAtom actor = TypedTupleGetElement(clauseActors, j);
									if(SameTypedAtoms(actor, termActor))
										TypedTupleSetAtom(clauseActors, j, inputParameter);
								}
							}
						}
					}
				}
				break;
			}

		}
		PrintCString("Updated clause: ");
		PrintFormActorsAsFormula(clauseForm, clauseActors);
		PrintChar('\n');

		FreeTypedTuple(serviceParameters);
		FreeTypedTuple(termActors);
		IFactRelease(negatedTermForm);
	}
	MultisetIteratorEnd(&termFormIterator);

	if(nTermsExcluded < clauseNTerms) {
		// Recurse on remaining terms
		Service * nextService = compileConjunctionRecursive(
			clauseForm, clauseActors, matchedTermIndex, nArguments,
			termExcluded, nTermsExcluded, termActorsIndices
		);
		if(nextService) {
			Service * joinService = CreateJoinService(service, nextService);
			ReleaseService(service);
			ReleaseService(nextService);
			return joinService;
		}
		else {
			// Failed to compile the rest of the cojnunction
			ReleaseService(service);
			return 0;
		}
	}
	else {
		// No more terms to consider, return the left child service
		// NOTE: this ends the recursion. 
		return service;
	}
}


static Service * compileConjunction(
	Atom clauseForm, TypedTuple * clauseActors, index8 matchedTermIndex, size8 nArguments)
{
	uint8 clauseNTerms = ClauseFormNTerms(clauseForm);
	index8 termActorsIndices[clauseNTerms + 1];
	ClauseGetTermActorsIndices(clauseForm, termActorsIndices);
	bool termExcluded[clauseNTerms];
	for(index8 i = 0; i < clauseNTerms; i++)
		termExcluded[i] = (i == matchedTermIndex);

	return compileConjunctionRecursive(
		clauseForm, clauseActors, matchedTermIndex, nArguments,
		termExcluded, 1, termActorsIndices);
}


/**
 * Attempt to compile a service with the given form and actors.
 * The queryActors tuple must be a series of AT_PARAMETER atoms numbered 1, 2, ... 
 * Parameter types in the queryActors tuple will be updated.
 * Returns the compiled service if successful, else 0.
 */
static Service * compileService(Atom queryTermForm, TypedTuple * queryActors)
{
	size8 termArity = TermFormArity(queryTermForm);

	// TODO: query existing services matching the term

	/**
	 * To find rules (clauses) c that contains a matching term form,
	 * we query (multiset c element @term-form multiple _),
	 * 
	 * If multiple rules match, we generate a UNION of
	 * the resulting services. The first clause that yields a service
	 * will determine the query parameter types; all other clauses must
	 * then yield services with those same types.
	 * 
	 * If the term occurs negated in a clause, then the clause is recursive.
	 * A recursive clause must always occur in a UNION with a least one
	 * non-recursive clause that provides the parameter types. 
	 * 
 	 * TODO: it might happen that a generated UNION service has the same
	 * signature as an existing service, which becomes part of the UNION.
	 * In this case, the newly generated service should replace the existing one.
	 * 
	 * NOTE: Recursive services are not guaranteed to terminate.
	 */

	Service * service = 0;

	// TODO: this must be the typed relation (multiset:ID element:ID multiple:UINT),
	// while for predicates roles we need (multiset:ID element:NAME multiple:UINT).
	RelationTable multisetTable = FindRelationTable(
		GetCorePredicateForm(FORM_MULTISET_ELEMENT_MULTIPLE),
		GetCorePredicateAtomTypes(FORM_MULTISET_ELEMENT_MULTIPLE)
	);

	RelationBTreeIterator btreeIterator;
	Atom multisetQueryTuple[3];
	// TODO: the element role of (multiset element multiple) may not be the
	// first column, so we have to table scan and filter.
	MultisetSetTuple(multisetQueryTuple, (Atom) {0}, queryTermForm, (Atom) {0});
	RelationBTreeIterate(&multisetTable, multisetQueryTuple, 1, &btreeIterator);
	while(RelationBTreeIteratorNext(&btreeIterator)) {
		// Found a multiset where the term form occurs
		Atom clauseForm = RelationBTreeIteratorGetAtom(
			&btreeIterator,
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)
		);
		size8 multiple = RelationBTreeIteratorGetAtom(
			&btreeIterator,
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)
		)._uint;
		// Ensure the multiset is a clause form
		if(!IsClauseForm(clauseForm))
			continue;

		// Iterate over all rules (clauses) with this clause form.
		DictionaryIterator dictIterator;
		DictionaryIterate(clauseForm, &dictIterator);
		TypedTuple * matchedTermActors = CreateTypedTuple(queryActors->nAtoms);
		TypedTuple * substClauseActors = CreateTypedTuple(ClauseArity(clauseForm));
		while(DictionaryIteratorNext(&dictIterator)) {
			TypedTuple const * clauseActors = DictionaryIteratorPeekActors(&dictIterator);
			PrintCString("Matched rule: ");
			PrintFormActorsAsFormula(clauseForm, clauseActors);
			PrintChar('\n');

			// Iterate over all occurences of the query term in the matched clause
			// and find one that unifies, if any.
			index8 matchedTermActorsIndex = ClauseGetTermActorsIndex(clauseForm, queryTermForm, 1);
			bool foundTerm = false;
			for(index8 m = 1; !foundTerm && (m <= multiple); m++) {
				// extract actors for the matching term in the clause
				TypedTupleCopyAt(clauseActors, matchedTermActorsIndex, matchedTermActors);
				// unify the query with the matched term
				Substitution querySubst;
				Substitution termSubst;
				foundTerm = UnifyTuples(queryActors, matchedTermActors, &querySubst, &termSubst);
				if(foundTerm) {
					SubstituteTuple(&termSubst, clauseActors, substClauseActors);
					PrintCString("Unified rule: ");
					PrintFormActorsAsFormula(clauseForm, substClauseActors);
					PrintChar('\n');

					index8 matchedTermIndex = ClauseGetTermIndex(clauseForm, queryTermForm, m);
					Service * newService = compileConjunction(
						clauseForm, substClauseActors, matchedTermIndex, termArity);
					if(newService) {
						// Update the query actors from the clause actors
						// to recover unified parameters
						// NOTE: when multiple services are matched, the resulting parameter tuple
						// must be identical across all services (same parameter types)
						TypedTupleCopyAt(substClauseActors, matchedTermActorsIndex, queryActors);
						// handle unions
						if(service) {
							Service * unionService = CreateUnionService(service, newService);
							ReleaseService(service);
							ReleaseService(newService);
							service = unionService;
						}
						else
							service = newService;
					}
				}
				FreeSubstitution(&querySubst);
				FreeSubstitution(&termSubst);
				matchedTermActorsIndex += termArity;
			}
		}
		DictionaryIteratorEnd(&dictIterator);
		FreeTypedTuple(substClauseActors);
		FreeTypedTuple(matchedTermActors);
	}
	RelationBTreeIteratorEnd(&btreeIterator);

	return service;
}


Service const * CompileService(Formula const * queryTerm)
{
	ASSERT(IsTermForm(queryTerm->form))
	// TypedTuple * queryActors = CreateTypedTuple(arity);
	// CopyListToTuple(FormulaGetActors(queryTerm), queryActors);

	// Generalize atoms in the query to parameters
	// NOTE: the compilation process will determine the type
	// of any output parameters. 
	TypedTuple * generalizedActors = CreateTypedTuple(queryTerm->actors->nAtoms);
	atomsToParameters(queryTerm->actors, generalizedActors);

	PrintCString("Generalized query: ");
	PrintFormActorsAsFormula(queryTerm->form, generalizedActors);
	PrintChar('\n');

	Service * service = compileService(queryTerm->form, generalizedActors);
	if(service) {
		// TODO: the service registry now expects a predicate form
		Atom const * serviceParameters = TypedTuplePeekAtoms(generalizedActors);
		RegistryAddService(queryTerm->form, serviceParameters, service);
		ReleaseService(service);
	}
	FreeTypedTuple(generalizedActors);
	return service;
}
