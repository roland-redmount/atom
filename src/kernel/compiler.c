#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/service.h"
#include "kernel/Parameter.h"
#include "kernel/RelationRegistry.h"
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
  * Note that y cannot simply be left out of the compiled services: it is shared
  * between the two terms, so the JOIN service needs it as an argument in order to
  * constrain one term against the other. We therefore give every such clause-local
  * variable an argument of its own, numbered after the query arguments, and compile
  * the conjunction with this extended arguments tuple. Since the local variables
  * are the trailing arguments, PROJECT need only keep a leading number of arguments.
  *
  * The PROJECT operation requires checking for duplicate tuples (unless the kept
  * arguments are known to be a unique key for the relation). This is problematic
  * since we want the service to yield one tuple at a time; currently PROJECT
  * enumerates its entire child relation when its context is created.
  * To enable efficient duplicate removal, the child service should yield tuples in
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
 * A query term that leaves an output parameter untyped may match several
 * services, one per relation table matching its form (the element type
 * of a list, say, is not determined by the query). Each such term is a choice
 * point, and each combination of choices yields a separately typed service.
 *
 * Rather than making every compilation step multi-valued, we keep them
 * single-valued and re-run the whole compilation once per combination,
 * forcing a different choice each time. A choice point is identified by the
 * position at which it is encountered, which is well defined because
 * DispatchQueryAt() enumerates candidates deterministically.
 */

#define MAX_CHOICE_POINTS	8

typedef struct s_ChoicePoints {
	// which match to take from DispatchQueryAt() at each choice point (0, 1, ...)
	index8 matchIndex[MAX_CHOICE_POINTS];
	// whether there is another match available at each choice point
	bool hasNextMatch[MAX_CHOICE_POINTS];
	// number of choice points encountered in the current run
	index8 depth;
} ChoicePoints;


static void resetChoicePoints(ChoicePoints * choices)
{
	SetMemory(choices, sizeof(ChoicePoints), 0);
}


/**
 * Advance to the next combination of choices, depth first: take the deepest
 * choice point that still has an untried alternative, and reset the choice points
 * below it. Returns false once all combinations have been visited.
 */
static bool nextChoiceBranch(ChoicePoints * choices)
{
	for(index8 i = choices->depth; i > 0; i--) {
		index8 d = i - 1;
		if(choices->hasNextMatch[d]) {
			choices->matchIndex[d]++;
			for(index8 j = i; j < MAX_CHOICE_POINTS; j++) {
				choices->matchIndex[j] = 0;
				choices->hasNextMatch[j] = false;
			}
			choices->depth = 0;
			return true;
		}
	}
	return false;
}


/**
 * Dispatch a term at the next choice point, taking the alternative selected
 * for the current branch and recording whether further alternatives exist.
 */
static bool dispatchAtChoicePoint(
	Atom termForm, TypedTuple const * termActors, ServiceRecord * record,
	index8 permutation[], ChoicePoints * choices)
{
	ASSERT(choices->depth < MAX_CHOICE_POINTS)
	index8 d = choices->depth++;
	return DispatchQueryAt(
		termForm, termActors, record, permutation,
		choices->matchIndex[d], &(choices->hasNextMatch[d])
	);
}


/**
 * Generate a parameters tuple from an actors tuple, such that each non-variable atom
 * in the actors tuple corresponds to an input parameter (with type preserved),
 * and each variable yields an output parameters. The output parameter types are
 * unknown and must be discovered later by matching against services.
 * The genererated parameter numbers are always equal to the tuple index (1-based).
 * 
 * NOTE: the parameters tuple could be an Atom[] as the type is constant, but this
 * currently doesn't fit with compileService() and downstream functions.
 */
static void actorsToParameters(TypedTuple const * actors, TypedTuple * parameters)
{
	for(index8 i = 0; i < actors->nAtoms; i++) {
		TypedAtom typedAtom = TypedTupleGetElement(actors, i);
		if(typedAtom.type == AT_VARIABLE) {
			Atom parameter = {
				.parameter = {.number = i + 1, .io = PARAMETER_OUT, .atomType = 0}
			};
			TypedTupleSetElement(parameters, i, CreateTypedAtom(AT_PARAMETER, parameter));
		}
		else {
			Atom parameter = {
				.parameter = {.number = i + 1, .io = PARAMETER_IN, .atomType = typedAtom.type}
			};
			TypedTupleSetElement(parameters, i, CreateTypedAtom(AT_PARAMETER, parameter));
		}
	}
}


/**
 * Merge the arguments of a compiled term that provide the same clause argument,
 * which happens when a variable occurs more than once in the term. Emits a
 * CONSTRAIN service yielding only those tuples in which the merged arguments are
 * equal, and compacts clauseMap accordingly, so that the clause arguments a term
 * provides are distinct. Takes over the caller's reference to the service.
 *
 * For example, a term whose four arguments provide the clause arguments
 * {2, 0, 2, 1} has its first and third argument merged, as both provide clause
 * argument 2. The constrain service then takes the argument map {0, 1, 0, 2}
 * and has three arguments, providing the clause arguments {2, 0, 1}.
 */
static Service * constrainRepeatedArguments(Service * service, index8 clauseMap[])
{
	size8 nChildArguments = service->nArguments;
	// The clause arguments as provided by the compiled term. We compact clauseMap
	// in place below, so we cannot look up earlier arguments in it.
	index8 termClauseMap[nChildArguments];
	CopyMemory(clauseMap, termClauseMap, nChildArguments * sizeof(index8));

	index8 argumentMap[nChildArguments];
	size8 nArguments = 0;
	for(index8 i = 0; i < nChildArguments; i++) {
		// an earlier argument providing the same clause argument shares its index
		argumentMap[i] = nArguments;
		for(index8 j = 0; j < i; j++) {
			if(termClauseMap[j] == termClauseMap[i]) {
				argumentMap[i] = argumentMap[j];
				break;
			}
		}
		if(argumentMap[i] == nArguments)
			clauseMap[nArguments++] = termClauseMap[i];
	}
	if(nArguments == nChildArguments)
		return service;

	Service * constrainService = CreateConstrainService(nArguments, argumentMap, service);
	ReleaseService(service);
	return constrainService;
}


/**
 * A term compiles to the service that dispatch matches to it, taking only the
 * arguments of the term itself. Any non-parameter actor is a constant restricting
 * one argument of that service, and is bound by a PERMUTE service wrapped around it;
 * a term without constants compiles to the matched service directly. The permutation
 * obtained from dispatch needs no service of its own: it is carried by the clauseMap.
 * (Variables occurring in the clause but not in the query are given parameter
 * numbers of their own by parameterizeLocalVariables() before we get here.)
 *
 * The clauseMap array is set to the clause argument provided by each argument of the
 * compiled service, and so has length equal to its nArguments. The caller places
 * those arguments into the clause arguments tuple, either as a child of a JOIN
 * service or, for a single term, with a PERMUTE service.
 *
 * The serviceParameters tuple is set to the matched service's parameters,
 * permuted to match the term actors order.
 */
static Service * compileTerm(
	Atom termForm, TypedTuple const * termActors,
	TypedTuple * serviceParameters, index8 clauseMap[], ChoicePoints * choices)
{
	// attempt to locate an service existing service
	size8 termArity = termActors->nAtoms;
	index8 permutation[termArity];
	ServiceRecord termServiceRecord;
	if(!dispatchAtChoicePoint(termForm, termActors, &termServiceRecord, permutation, choices))
		return 0;

	// Count the constants first: a permute service indexes its constants after
	// its arguments, so we need the number of arguments before we can map them.
	size8 nConstants = 0;
	for(index8 i = 0; i < termArity; i++) {
		if(TypedTupleGetElement(termActors, permutation[i]).type != AT_PARAMETER)
			nConstants++;
	}
	size8 nArguments = termArity - nConstants;

	// Compute the argument map for each service parameter, respecting the argument
	// permutation obtained from DispatchQuery() above
	index8 argumentMap[termArity];
	Atom constants[termArity];
	byte constantTypes[termArity];
	size8 nMapped = 0;
	size8 nMappedConstants = 0;
	// loop over service parameters
	for(index8 i = 0; i < termArity; i++) {
		TypedAtom actor = TypedTupleGetElement(termActors, permutation[i]);
		if(actor.type == AT_PARAMETER) {
			argumentMap[i] = nMapped;
			// parameter numbers are 1-based positions, argument maps are 0-based indices
			clauseMap[nMapped] = actor.atom.parameter.number - 1;
			nMapped++;
		}
		else {
			// a constant restricting this service argument
			ASSERT(actor.type != AT_VARIABLE)
			argumentMap[i] = nArguments + nMappedConstants;
			constants[nMappedConstants] = actor.atom;
			constantTypes[nMappedConstants] = actor.type;
			nMappedConstants++;
		}
		TypedTupleSetElement(serviceParameters, permutation[i],
			CreateTypedAtom(
				AT_PARAMETER,
				(Atom) {
					.parameter = {
						.number = i + 1,
						.atomType = termServiceRecord.relation->atomTypes[i],
						.io = termServiceRecord.parameterIO[i]
					}
				}
			)
		);
	}
	ASSERT(nMapped == nArguments)

	Service * service;
	if(!nConstants) {
		// Without constants to bind, the matched service is used as it is
		AcquireService(termServiceRecord.service);
		service = termServiceRecord.service;
	}
	else {
		service = CreatePermuteService(
			nArguments, constants, constantTypes, nConstants, argumentMap,
			termServiceRecord.service);
	}
	// A variable occurring more than once in the term constrains the arguments
	// providing it to be equal
	return constrainRepeatedArguments(service, clauseMap);
}


/**
 * Determine the arguments of a JOIN service from the clause arguments its two child
 * services provide, and compute the argument map of each child into the join arguments
 * tuple. A join numbers its arguments by the clause arguments it covers, in ascending
 * order, so that the outermost join of a conjunction ends up with the clause arguments
 * in their own order. Returns the number of join arguments.
 */
static size8 setupJoinArgumentMaps(
	size8 clauseNArguments,
	index8 const leftClauseMap[], size8 nLeftArguments,
	index8 const rightClauseMap[], size8 nRightArguments,
	index8 clauseMap[], index8 leftMap[], index8 rightMap[])
{
	bool covered[clauseNArguments];
	SetMemory(covered, clauseNArguments * sizeof(bool), 0);
	for(index8 i = 0; i < nLeftArguments; i++)
		covered[leftClauseMap[i]] = true;
	for(index8 i = 0; i < nRightArguments; i++)
		covered[rightClauseMap[i]] = true;

	// Number the covered clause arguments in ascending order
	index8 joinArgument[clauseNArguments];
	size8 nArguments = 0;
	for(index8 i = 0; i < clauseNArguments; i++) {
		if(covered[i]) {
			joinArgument[i] = nArguments;
			clauseMap[nArguments] = i;
			nArguments++;
		}
	}
	for(index8 i = 0; i < nLeftArguments; i++)
		leftMap[i] = joinArgument[leftClauseMap[i]];
	for(index8 i = 0; i < nRightArguments; i++)
		rightMap[i] = joinArgument[rightClauseMap[i]];
	return nArguments;
}


/**
 * Compile a JOIN service from the conjuction obtained by negating the given clause
 * (clauseForm, clauseActors). The query-matched term indicated by matchedTermIndex
 * is excluded from compilation; also, any term t that has already been compiled
 * is indicated by termExcluded[t] = true.
 * clauseNArguments is the total number of parameters in the clause, including "local" variables.
 *
 * We iterate over all terms (negated) until we find a term that dispatches to a known service;
 * we then return a JOIN service between this service and the service obtained by recursively
 * compiling the remaining terms.
 * If the clause contains only 1 term besides the query term, we emit its service directly
 * without a JOIN, terminating the recursion.
 *
 * The compiled service takes only the clause arguments its terms provide, and the
 * clauseMap array is set to the clause argument provided by each of its arguments.
 */
static Service * compileConjunctionRecursive(
	Atom clauseForm, TypedTuple * clauseActors, index8 matchedTermIndex, size8 clauseNArguments,
	bool termExcluded[], uint8 nTermsExcluded, index8 const termActorsIndices[],
	index8 clauseMap[], ChoicePoints * choices)
{
	uint8 clauseNTerms = ClauseFormNTerms(clauseForm);
	ASSERT(clauseNTerms >= 2)
	Service * service = 0;
	// Clause arguments provided by the compiled term. A term may refer to the same
	// clause argument more than once, so it may have more arguments than the clause.
	index8 termClauseMap[clauseActors->nAtoms];
	// Arity of the query-matched term. Parameters with number > matchedTermArity
	// are clause-local variables that not occur in the query term.
	size8 matchedTermArity = termActorsIndices[matchedTermIndex + 1] - termActorsIndices[matchedTermIndex];

	// Find a term that can be compiled.
	// First iterate over term forms in the clause form
	MultisetIterator termFormIterator;
	MultisetIterate(clauseForm, AT_ID, &termFormIterator);
	size8 termIndex = 0;
	while(!service && nTermsExcluded < clauseNTerms && MultisetIteratorNext(&termFormIterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&termFormIterator);
		if(termIndex == matchedTermIndex) {
			termIndex += em.multiple;
			continue;
		}
		Atom termForm = em.element;
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
			// Attempt to compile this term to a Service
			service = compileTerm(
				negatedTermForm, termActors, serviceParameters, termClauseMap, choices);
			PrintCString("serviceParameters = ");
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
							// Replace untyped output parameter in query matched term.
							// A clause-local parameter has no counterpart there.
							if(queryTermParameterNr <= matchedTermArity) {
								index8 queryParameterIndex =
									termActorsIndices[matchedTermIndex] + queryTermParameterNr - 1;
								TypedTupleSetAtom(clauseActors, queryParameterIndex, outputParameter);
							}
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

	if(!service) {
		// No remaining term could be dispatched
		return 0;
	}

	if(nTermsExcluded < clauseNTerms) {
		// Recurse on remaining terms.
		index8 nextClauseMap[clauseActors->nAtoms];
		Service * nextService = compileConjunctionRecursive(
			clauseForm, clauseActors, matchedTermIndex, clauseNArguments,
			termExcluded, nTermsExcluded, termActorsIndices, nextClauseMap, choices
		);
		if(nextService) {
			// The two child services provide the clause arguments of their own terms,
			// which the argument maps place into the join arguments tuple
			index8 leftMap[service->nArguments];
			index8 rightMap[nextService->nArguments];
			size8 nJoinArguments = setupJoinArgumentMaps(
				clauseNArguments,
				termClauseMap, service->nArguments,
				nextClauseMap, nextService->nArguments,
				clauseMap, leftMap, rightMap
			);
			Service * joinService = CreateJoinService(
				nJoinArguments, service, leftMap, nextService, rightMap);
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
		CopyMemory(termClauseMap, clauseMap, service->nArguments * sizeof(index8));
		return service;
	}
}


/**
 * Create parameters for every "local" variable occurring in the clause but not in the
 * query-matched term. The new parametesr are numbered consecutively after the query parameters.
 * Local variables are shared between the terms of the conjunction, and so must have a column in the
 * arguments tuple so that the JOIN service can constrain terms against each other.
 * After compiling the JOIN, a PROJECT service is used to drop these trailing columns.
 * Returns the number of local variables found.
 *
 * NOTE: each occurence of the anonymous variable _ is a variable of its own,
 * and so obtains a parameter of its own. SameVariable() gives us this for free.
 */
static size8 parameterizeLocalVariables(
	TypedTuple * clauseActors, index8 matchedTermIndex, index8 const * termActorsIndices,
	size8 matchedTermArity)
{
	index8 matchedTermBegin = termActorsIndices[matchedTermIndex];
	index8 matchedTermEnd = termActorsIndices[matchedTermIndex + 1];
	size8 nLocalVariables = 0;

	for(index8 i = 0; i < clauseActors->nAtoms; i++) {
		if((i >= matchedTermBegin) && (i < matchedTermEnd))
			continue;
		TypedAtom actor = TypedTupleGetElement(clauseActors, i);
		if(actor.type != AT_VARIABLE)
			continue;
		// The parameter type is unknown here, and is resolved by
		// compileConjunctionRecursive() once a term producing it has compiled.
		TypedAtom parameter = CreateTypedAtom(
			AT_PARAMETER,
			(Atom) {
				.parameter = {
					.number = matchedTermArity + (++nLocalVariables),
					.io = PARAMETER_OUT,
					.atomType = 0
				}
			}
		);
		// Replace this occurence of the variable, and any remaining ones
		TypedTupleSetElement(clauseActors, i, parameter);
		for(index8 j = i + 1; j < clauseActors->nAtoms; j++) {
			if((j >= matchedTermBegin) && (j < matchedTermEnd))
				continue;
			TypedAtom other = TypedTupleGetElement(clauseActors, j);
			if((other.type == AT_VARIABLE) && SameVariable(other.atom, actor.atom))
				TypedTupleSetElement(clauseActors, j, parameter);
		}
	}
	return nLocalVariables;
}


/**
 * Rearrange the arguments of a compiled conjunction into the clause argument order,
 * emitting a PERMUTE service unless they are in that order already. Takes over the
 * caller's reference to the given service.
 *
 * The terms of the conjunction must together provide every clause argument. If they
 * do not, the clause cannot yield a valid relation: the arguments no term provides
 * would be left undefined. This is not a program error but an invalid rule, so we
 * release the service and return 0.
 */
static Service * permuteToClauseArguments(
	Service * service, index8 const clauseMap[], size8 clauseNArguments)
{
	bool covered[clauseNArguments];
	SetMemory(covered, clauseNArguments * sizeof(bool), 0);
	bool ordered = (service->nArguments == clauseNArguments);
	for(index8 i = 0; i < service->nArguments; i++) {
		covered[clauseMap[i]] = true;
		if(clauseMap[i] != i)
			ordered = false;
	}
	for(index8 i = 0; i < clauseNArguments; i++) {
		if(!covered[i]) {
			PrintCString("Clause does not provide every argument\n");
			ReleaseService(service);
			return 0;
		}
	}
	if(ordered)
		return service;

	Service * permuteService = CreatePermuteService(
		clauseNArguments, 0, 0, 0, clauseMap, service);
	ReleaseService(service);
	return permuteService;
}


/**
 * Compile the conjunction formed by negating the given clause (clauseForm, clauseActors),
 * excepting the term matching the query, indicated by matchedTermIndex.
 */
static Service * compileConjunction(
	Atom clauseForm, TypedTuple * clauseActors, index8 matchedTermIndex, size8 nArguments,
	ChoicePoints * choices)
{
	uint8 clauseNTerms = ClauseFormNTerms(clauseForm);
	index8 termActorsIndices[clauseNTerms + 1];
	ClauseGetTermActorsIndices(clauseForm, termActorsIndices);
	bool termExcluded[clauseNTerms];
	for(index8 i = 0; i < clauseNTerms; i++)
		termExcluded[i] = (i == matchedTermIndex);

	// Clause-local variables are variables (AT_VARIABLE atoms) in the clause actors
	// that are not present in the query-matched term. These become additional parameters,
	// and the conjunction is compiled with this extended arguments tuple.
	size8 nLocalVariables = parameterizeLocalVariables(
		clauseActors, matchedTermIndex, termActorsIndices, nArguments);
	size8 clauseNArguments = nArguments + nLocalVariables;

	index8 clauseMap[clauseActors->nAtoms];
	Service * service = compileConjunctionRecursive(
		clauseForm, clauseActors, matchedTermIndex, clauseNArguments,
		termExcluded, 1, termActorsIndices, clauseMap, choices);
	if(!service)
		return 0;

	// The compiled terms provide the clause arguments in their own order
	service = permuteToClauseArguments(service, clauseMap, clauseNArguments);

	// Drop the local variable arguments again, and any duplicate tuples this creates
	if(service && nLocalVariables) {
		Service * projectService = CreateProjectService(service, nArguments);
		ReleaseService(service);
		service = projectService;
	}
	return service;
}


/**
 * A compiled service together with the query parameters it resolved to.
 * One variant is emitted per distinct parameter signature; clauses that
 * resolve to the same signature are combined into a UNION, as before.
 */
typedef struct s_CompiledVariant {
	// resolved query parameters, owned by the variant
	TypedTuple * parameters;
	Service * service;
} CompiledVariant;


/**
 * Two parameter tuples denote the same service signature if they agree on
 * the type and direction of every parameter; parameter numbers are ignored here.
 */
static bool sameParameterSignature(TypedTuple const * first, TypedTuple const * second)
{
	ASSERT(first->nAtoms == second->nAtoms)
	for(index8 i = 0; i < first->nAtoms; i++) {
		TypedAtom a = TypedTupleGetElement(first, i);
		TypedAtom b = TypedTupleGetElement(second, i);
		ASSERT((a.type == AT_PARAMETER) && (b.type == AT_PARAMETER))
		if(a.atom.parameter.atomType != b.atom.parameter.atomType)
			return false;
		if(a.atom.parameter.io != b.atom.parameter.io)
			return false;
	}
	return true;
}


static CompiledVariant * findVariant(
	CompiledVariant * variants, size8 nVariants, TypedTuple const * parameters)
{
	for(index8 i = 0; i < nVariants; i++) {
		if(sameParameterSignature(variants[i].parameters, parameters))
			return &(variants[i]);
	}
	return 0;
}


/**
 * Attempt to compile services with the given form and actors.
 * The queryActors tuple must be a series of AT_PARAMETER atoms numbered 1, 2, ...
 * and is not modified; each compiled variant carries its own resolved parameters.
 * Returns the number of variants written to the variants array.
 */
static size8 compileService(
	Atom queryTermForm, TypedTuple const * queryActors,
	CompiledVariant * variants, size8 maxVariants)
{
	size8 queryTermArity = TermFormArity(queryTermForm);
	size8 nVariants = 0;

	// TODO: query existing services matching the term

	/**
	 * To find rules (clauses) c that contains a matching term form,
	 * we query (multiset c element @term-form multiple m),
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

	/**
	 * TODO: here we need the service (multiset >ID element <ID multiple >UINT) where element is input
	 * Since the element role is not a leading column, RelationBTree does not support this.
	 * For now, we simply scan the entire table and filter on matching terms. This is obviously
	 * highly inefficient. A better solution would require multiple indexes on the relation table.
	 */
	Service const * multisetService = GetCoreService(SERVICE_MULTISET_ID_ALL);

	Atom multisetQueryTuple[3];
	ServiceContext * multisetContext = ServiceCreateContext(multisetService, multisetQueryTuple);
	while(ServiceCall(multisetContext)) {
		Atom termForm = multisetQueryTuple[
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT)];
		if(termForm.hash != queryTermForm.hash)
			continue;
		// Found a multiset where the term form occurs
		Atom clauseForm = multisetQueryTuple[
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)];
		size8 multiple = multisetQueryTuple[
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)]._uint;
		// Ensure the multiset is a clause form
		if(!IsClauseForm(clauseForm))
			continue;

		// Iterate over all rules (clauses) with this clause form.
		DictionaryIterator dictIterator;
		DictionaryIterate(clauseForm, &dictIterator);
		TypedTuple * matchedTermActors = CreateTypedTuple(queryTermArity);
		TypedTuple * substClauseActors = CreateTypedTuple(ClauseArity(clauseForm));
		TypedTuple * resolvedParameters = CreateTypedTuple(queryTermArity);
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
				Substitution matchedTermSubst;
				foundTerm = UnifyTuples(queryActors, matchedTermActors, &querySubst, &matchedTermSubst);
				if(foundTerm) {
					index8 matchedTermIndex = ClauseGetTermIndex(clauseForm, queryTermForm, m);
					// Compile once per combination of choices. A term that leaves
					// an output parameter untyped may match several services, each
					// yielding a differently typed variant of the query service.
					ChoicePoints choices;
					resetChoicePoints(&choices);
					do {
						// compileConjunction() updates parameter types in the clause
						// actors, so re-derive them for each branch.
						SubstituteTuple(&matchedTermSubst, clauseActors, substClauseActors);
						PrintCString("Unified rule: ");
						PrintFormActorsAsFormula(clauseForm, substClauseActors);
						PrintChar('\n');

						Service * newService = compileConjunction(
							clauseForm, substClauseActors, matchedTermIndex, queryTermArity, &choices);
						if(!newService)
							continue;
						// Recover the unified parameters from the clause actors
						TypedTupleCopyAt(substClauseActors, matchedTermActorsIndex, resolvedParameters);
						// Check for previously compiled service with the same signature
						CompiledVariant * variant = findVariant(variants, nVariants, resolvedParameters);
						if(variant) {
							// Another clause yielded the same signature: union them
							Service * unionService = CreateUnionService(variant->service, newService);
							ReleaseService(variant->service);
							ReleaseService(newService);
							variant->service = unionService;
						}
						else {
							ASSERT(nVariants < maxVariants)
							variants[nVariants].parameters = CreateTypedTuple(queryTermArity);
							TypedTupleCopy(resolvedParameters, variants[nVariants].parameters);
							variants[nVariants].service = newService;
							nVariants++;
						}
					} while(nextChoiceBranch(&choices));
				}
				FreeSubstitution(&querySubst);
				FreeSubstitution(&matchedTermSubst);
				matchedTermActorsIndex += queryTermArity;
			}
		}
		DictionaryIteratorEnd(&dictIterator);
		FreeTypedTuple(resolvedParameters);
		FreeTypedTuple(substClauseActors);
		FreeTypedTuple(matchedTermActors);
	}
	ServiceFreeContext(multisetContext);

	return nVariants;
}


size8 CompileService(Formula const * queryTerm, ServiceRecord records[], size8 maxRecords)
{
	ASSERT(IsTermForm(queryTerm->form))
	ASSERT(maxRecords > 0)

	// Generalize atoms in the query to parameters
	size8 arity = queryTerm->actors->nAtoms;
	TypedTuple * queryParameters = CreateTypedTuple(arity);
	actorsToParameters(queryTerm->actors, queryParameters);

	PrintCString("\nCompileService()\nqueryParameters: ");
	PrintFormActorsAsFormula(queryTerm->form, queryParameters);
	PrintChar('\n');

	CompiledVariant variants[maxRecords];
	size8 nVariants = compileService(queryTerm->form, queryParameters, variants, maxRecords);

	for(index8 i = 0; i < nVariants; i++) {
		// Parameter types were resolved by compileService()
		Atom const * serviceParameters = TypedTuplePeekAtoms(variants[i].parameters);
		byte atomTypes[arity];
		byte parameterIO[arity];
		for(index8 j = 0; j < arity; j++) {
			atomTypes[j] = serviceParameters[j].parameter.atomType;
			parameterIO[j] = serviceParameters[j].parameter.io;
		}
		// Reuse the relation table if this signature has been compiled before
		RelationTable const * relation = RelationRegistryFind(queryTerm->form, arity, atomTypes);
		if(!relation) {
			relation = CreateRelationTable(0, queryTerm->form, arity, atomTypes, 0);
			ASSERT(relation)
			RelationRegistryAdd(relation);
		}
		records[i] = ServiceRegistryAdd(relation, parameterIO, variants[i].service);
		ReleaseService(variants[i].service);
		FreeTypedTuple(variants[i].parameters);
	}
	FreeTypedTuple(queryParameters);

	PrintCString("-> compiled services:\n");
	for(index8 i = 0; i < nVariants; i++) {
		PrintServiceRecord(&records[i]);
		PrintChar('\n');
	}

	return nVariants;
}
