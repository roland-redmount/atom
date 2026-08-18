#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/operator.h"
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


/**
 * The compiler turns a query that no service answers into a new service, by resolving it
 * against the rules in the dictionary and compiling the result into an operator tree.
 * CompileQuery() is the entry point.
 *
 * See compiler.md in the repository root for what the compiler does and why: the worked
 * examples behind each operator it emits, how a recursive rule compiles to a fixpoint,
 * and what is known not to work yet.
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
	Atom termForm, TypedTuple const * termActors, Service * service,
	index8 permutation[], ChoicePoints * choices)
{
	ASSERT(choices->depth < MAX_CHOICE_POINTS)
	index8 d = choices->depth++;
	return DispatchQueryAt(
		termForm, termActors, service, permutation,
		choices->matchIndex[d], &(choices->hasNextMatch[d])
	);
}


void GetQueryParameters(TypedTuple const * actors, TypedTuple * parameters)
{
	ASSERT(actors->nAtoms == parameters->nAtoms)
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
 * CONSTRAIN operator yielding only those tuples in which the merged arguments are
 * equal, and compacts clauseMap accordingly, so that the clause arguments a term
 * provides are distinct. Takes over the caller's reference to the operator.
 *
 * For example, a term whose four arguments provide the clause arguments
 * {2, 0, 2, 1} has its first and third argument merged, as both provide clause
 * argument 2. The constrain operator then takes the argument map {0, 1, 0, 2}
 * and has three arguments, providing the clause arguments {2, 0, 1}.
 */
static Operator * constrainRepeatedArguments(Operator * op, index8 clauseMap[])
{
	size8 nChildArguments = op->nArguments;
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
		return op;

	Operator * constrainOperator = CreateConstrainOperator(nArguments, argumentMap, op);
	ReleaseOperator(op);
	return constrainOperator;
}


/**
 * A term compiles to the service that dispatch matches to it, taking only the
 * arguments of the term itself. Any non-parameter actor is a constant restricting
 * one argument of that service, and is bound by a PERMUTE operator wrapped around it;
 * a term without constants compiles to the matched service directly. The permutation
 * obtained from dispatch needs no operator of its own: it is carried by the clauseMap.
 * (Variables occurring in the clause but not in the query are given parameter
 * numbers of their own by parameterizeLocalVariables() before we get here.)
 *
 * The clauseMap array is set to the clause argument provided by each argument of the
 * compiled operator, and so has length equal to its nArguments. The caller places
 * those arguments into the clause arguments tuple, either as a child of a JOIN
 * operator or, for a single term, with a PERMUTE operator.
 *
 * The serviceParameters tuple is set to the matched service's parameters,
 * permuted to match the term actors order.
 */
static Operator * compileTerm(
	Atom termForm, TypedTuple const * termActors,
	TypedTuple * serviceParameters, index8 clauseMap[], ChoicePoints * choices)
{
	// attempt to locate an existing service
	size8 termArity = termActors->nAtoms;
	index8 permutation[termArity];
	Service termService;
	if(!dispatchAtChoicePoint(termForm, termActors, &termService, permutation, choices))
		return 0;

	// Count the constants first: a permute operator indexes its constants after
	// its arguments, so we need the number of arguments before we can map them.
	size8 nConstants = 0;
	for(index8 i = 0; i < termArity; i++) {
		if(TypedTupleGetElement(termActors, permutation[i]).type != AT_PARAMETER)
			nConstants++;
	}
	size8 nArguments = termArity - nConstants;

	// Compute the argument map for each service parameter, respecting the argument
	// permutation obtained from dispatch above
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
						.atomType = termService.relation->atomTypes[i],
						.io = termService.parameterIO[i]
					}
				}
			)
		);
	}
	ASSERT(nMapped == nArguments)

	Operator * op;
	if(!nConstants) {
		// Without constants to bind, the matched service is used as it is
		AcquireOperator(termService.op);
		op = termService.op;
	}
	else {
		op = CreatePermuteOperator(
			nArguments, constants, constantTypes, nConstants, argumentMap,
			termService.op);
	}
	// A variable occurring more than once in the term constrains the arguments
	// providing it to be equal
	return constrainRepeatedArguments(op, clauseMap);
}


/**
 * Determine the arguments of a JOIN operator from the clause arguments its two child
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
 * The state of compiling one clause into a conjunction of operators, shared by the
 * recursion over its terms. Both the actors and the excluded flags are updated as terms
 * compile: an actor is given its atom type once a term providing it has dispatched, and
 * a term is marked excluded once it has been compiled.
 */
typedef struct s_ClauseCompileState {
	Atom form;
	TypedTuple * actors;
	// Index into actors of the first actor of each term, with a final entry for the end
	index8 const * termActorsIndices;
	uint8 nTerms;
	// Total number of clause arguments, including the local variables
	size8 nArguments;
	// The term the query matched, which is not part of the conjunction
	index8 matchedTermIndex;
	// The form of that term, which is also the form of a recursive term in the clause
	Atom queryTermForm;
	// Arity of that term. A parameter numbered beyond it is a clause-local variable,
	// which does not occur in the query term.
	size8 matchedTermArity;
	// The terms compiled so far, together with the matched term
	bool * termExcluded;
	ChoicePoints * choices;
} ClauseCompileState;


/**
 * Write the parameter types a compiled term resolved back into the clause. Dispatching a
 * term gives its untyped output parameters the types of the service that matched, and the
 * terms sharing those parameters need to know them:
 *
 *  - in the query-matched term the parameter stays an output, and so gives the service
 *    being compiled its signature;
 *  - in the terms not yet compiled it becomes an input, as the term that just compiled
 *    is what provides it.
 *
 * The term that compiled must already be marked excluded, so that it is not mistaken for
 * one of the terms still to come.
 */
static void propagateTermParameterTypes(
	ClauseCompileState * clauseState, index8 termIndex,
	TypedTuple const * termActors, TypedTuple const * serviceParameters)
{
	ASSERT(clauseState->termExcluded[termIndex])
	for(index8 i = 0; i < termActors->nAtoms; i++) {
		TypedAtom termActor = TypedTupleGetElement(termActors, i);
		if((termActor.type != AT_PARAMETER) || termActor.atom.parameter.atomType)
			continue;

		// The corresponding service parameter must be a typed output
		TypedAtom serviceParameter = TypedTupleGetElement(serviceParameters, i);
		ASSERT(serviceParameter.type == AT_PARAMETER)
		ASSERT(serviceParameter.atom.parameter.io == PARAMETER_OUT)
		byte parameterType = serviceParameter.atom.parameter.atomType;
		ASSERT(parameterType)

		index8 parameterNumber = termActor.atom.parameter.number;
		Atom inputParameter = {
			.parameter = {
				.number = parameterNumber, .io = PARAMETER_IN, .atomType = parameterType
			}
		};
		Atom outputParameter = {
			.parameter = {
				.number = parameterNumber, .io = PARAMETER_OUT, .atomType = parameterType
			}
		};

		// Type the parameter in the query-matched term, unless it is a clause-local
		// variable, which has no counterpart there
		if(parameterNumber <= clauseState->matchedTermArity) {
			index8 matchedParameterIndex =
				clauseState->termActorsIndices[clauseState->matchedTermIndex] + parameterNumber - 1;
			TypedTupleSetAtom(clauseState->actors, matchedParameterIndex, outputParameter);
		}
		// Type the parameter in the term that compiled
		// NOTE: not necessary, this term is not used for anything at this point
		TypedTupleSetAtom(
			clauseState->actors, clauseState->termActorsIndices[termIndex] + i, outputParameter);

		// The terms still to compile take the parameter as an input
		for(index8 j = 0; j < clauseState->nTerms; j++) {
			if(clauseState->termExcluded[j])
				continue;
			index8 termEnd = clauseState->termActorsIndices[j + 1];
			for(index8 k = clauseState->termActorsIndices[j]; k < termEnd; k++) {
				TypedAtom actor = TypedTupleGetElement(clauseState->actors, k);
				if(SameTypedAtoms(actor, termActor))
					TypedTupleSetAtom(clauseState->actors, k, inputParameter);
			}
		}
	}
}


/**
 * Compile a JOIN operator from the conjuction obtained by negating the clause being
 * compiled, excluding the term the query matched and any term compiled already.
 * nTermsExcluded counts those terms.
 *
 * We iterate over all terms (negated) until we find a term that dispatches to a known service;
 * we then return a JOIN operator between this operator and the operator obtained by recursively
 * compiling the remaining terms.
 * If the clause contains only 1 term besides the query term, we emit its service directly
 * without a JOIN, terminating the recursion.
 *
 * The compiled service takes only the clause arguments its terms provide, and the
 * clauseMap array is set to the clause argument provided by each of its arguments.
 */
static Operator * compileConjunctionRecursive(
	ClauseCompileState * clauseState, uint8 nTermsExcluded, index8 clauseMap[])
{
	ASSERT(clauseState->nTerms >= 2)
	Operator * op = 0;
	// Clause arguments provided by the compiled term. A term may refer to the same
	// clause argument more than once, so it may have more arguments than the clause.
	index8 termClauseMap[clauseState->actors->nAtoms];

	/**
	 * Find a term that can be compiled, in two passes over the term forms of the clause.
	 * The first pass skips the recursive term, the second takes it.
	 *
	 * A recursive term is deferred because it dispatches no matter what is bound: it has a
	 * temporary service for every IO pattern of the query, as registerTemporaryServices()
	 * cannot know in advance which one the clause needs. It would therefore win over a term
	 * whose outputs it should be consuming, leaving that term with an input the relation
	 * it reads has no service for. Every other term dispatches only once its own inputs
	 * are available, which is what makes taking the first one that compiles sound.
	 */
	for(index8 pass = 0; !op && (pass < 2); pass++) {
		// Iterate over term forms in the clause form
		MultisetIterator termFormIterator;
		MultisetIterate(clauseState->form, AT_ID, &termFormIterator);
		size8 termIndex = 0;
		while(!op && nTermsExcluded < clauseState->nTerms && MultisetIteratorNext(&termFormIterator)) {
			ElementMultiple em = MultisetIteratorGetElement(&termFormIterator);
			if(termIndex == clauseState->matchedTermIndex) {
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
			// A term of the query's own form is the recursive one
			if((pass == 0) && (negatedTermForm.hash == clauseState->queryTermForm.hash)) {
				termIndex += em.multiple;
				IFactRelease(negatedTermForm);
				continue;
			}
			// iterate over all terms (multiples) of this form
			TypedTuple * termActors = CreateTypedTuple(termArity);
			TypedTuple * serviceParameters = CreateTypedTuple(termArity);
			for(index8 m = 0; m < em.multiple; m++, termIndex++) {
				if(clauseState->termExcluded[termIndex])
					continue;
				// Extract term actors
				TypedTupleCopyAt(clauseState->actors, clauseState->termActorsIndices[termIndex], termActors);
				PrintCString("Term: ");
				PrintFormActorsAsFormula(negatedTermForm, termActors);
				PrintChar('\n');
				// Attempt to compile this term to an Service
				op = compileTerm(
					negatedTermForm, termActors, serviceParameters, termClauseMap, clauseState->choices);
				PrintCString("serviceParameters = ");
				TypedTuplePrint(serviceParameters);
				PrintChar('\n');

				if(op) {
					clauseState->termExcluded[termIndex] = true;
					nTermsExcluded++;
					propagateTermParameterTypes(clauseState, termIndex, termActors, serviceParameters);
					PrintCString("Updated clause: ");
					PrintFormActorsAsFormula(clauseState->form, clauseState->actors);
					PrintChar('\n');
					break;
				}
			}
			FreeTypedTuple(serviceParameters);
			FreeTypedTuple(termActors);
			IFactRelease(negatedTermForm);
		}
		MultisetIteratorEnd(&termFormIterator);
	}

	if(!op) {
		// No remaining term could be dispatched
		return 0;
	}

	if(nTermsExcluded < clauseState->nTerms) {
		// Recurse on remaining terms.
		index8 nextClauseMap[clauseState->actors->nAtoms];
		Operator * nextOperator = compileConjunctionRecursive(
			clauseState, nTermsExcluded, nextClauseMap);
		if(nextOperator) {
			// The two child operators provide the clause arguments of their own terms,
			// which the argument maps place into the join arguments tuple
			index8 leftMap[op->nArguments];
			index8 rightMap[nextOperator->nArguments];
			size8 nJoinArguments = setupJoinArgumentMaps(
				clauseState->nArguments,
				termClauseMap, op->nArguments,
				nextClauseMap, nextOperator->nArguments,
				clauseMap, leftMap, rightMap
			);
			Operator * joinOperator = CreateJoinOperator(
				nJoinArguments, op, leftMap, nextOperator, rightMap);
			ReleaseOperator(op);
			ReleaseOperator(nextOperator);
			return joinOperator;
		}
		else {
			// Failed to compile the rest of the cojnunction
			ReleaseOperator(op);
			return 0;
		}
	}
	else {
		// No more terms to consider, return the left child operator
		// NOTE: this ends the recursion.
		CopyMemory(termClauseMap, clauseMap, op->nArguments * sizeof(index8));
		return op;
	}
}


/**
 * Create parameters for every "local" variable occurring in the clause but not in the
 * query-matched term. The new parameters are numbered consecutively after the query parameters.
 * Local variables are shared between the terms of the conjunction, and so must have a column in the
 * arguments tuple so that the JOIN operator can constrain terms against each other.
 * After compiling the JOIN, a PROJECT operator is used to drop these trailing columns.
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
 * emitting a PERMUTE operator unless they are in that order already. Takes over the
 * caller's reference to the given service; the caller instead obtains a reference
 * to the returned Service.
 *
 * The terms of the conjunction must together provide every clause argument. If they
 * do not, the clause cannot yield a valid relation: the arguments no term provides
 * would be left undefined. This is not a program error but an invalid rule, so we
 * release the service and return 0.
 */
static Operator * permuteToClauseArguments(
	Operator * op, index8 const clauseMap[], size8 clauseNArguments)
{
	bool covered[clauseNArguments];
	SetMemory(covered, clauseNArguments * sizeof(bool), 0);
	bool ordered = (op->nArguments == clauseNArguments);
	for(index8 i = 0; i < op->nArguments; i++) {
		covered[clauseMap[i]] = true;
		if(clauseMap[i] != i)
			ordered = false;
	}
	for(index8 i = 0; i < clauseNArguments; i++) {
		if(!covered[i]) {
			PrintCString("Clause does not provide every argument\n");
			ReleaseOperator(op);
			return 0;
		}
	}
	if(ordered)
		return op;

	Operator * permuteOperator = CreatePermuteOperator(
		clauseNArguments, 0, 0, 0, clauseMap, op);
	ReleaseOperator(op);
	return permuteOperator;
}


/**
 * Compile the conjunction formed by negating the given clause (clauseForm, clauseActors),
 * excepting the term matching the query, indicated by matchedTermIndex.
 */
static Operator * compileConjunction(
	Atom clauseForm, TypedTuple * clauseActors, index8 matchedTermIndex, Atom queryTermForm,
	size8 nArguments, ChoicePoints * choices)
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

	ClauseCompileState clauseState = {
		.form = clauseForm,
		.actors = clauseActors,
		.termActorsIndices = termActorsIndices,
		.nTerms = clauseNTerms,
		.nArguments = nArguments + nLocalVariables,
		.matchedTermIndex = matchedTermIndex,
		.queryTermForm = queryTermForm,
		.matchedTermArity =
			termActorsIndices[matchedTermIndex + 1] - termActorsIndices[matchedTermIndex],
		.termExcluded = termExcluded,
		.choices = choices
	};

	index8 clauseMap[clauseActors->nAtoms];
	// The matched term is excluded from the conjunction, and is the one term excluded
	// when the recursion over the remaining terms begins
	Operator * op = compileConjunctionRecursive(&clauseState, 1, clauseMap);
	if(!op)
		return 0;

	// The compiled terms provide the clause arguments in their own order
	op = permuteToClauseArguments(op, clauseMap, clauseState.nArguments);

	// Drop the local variable arguments again, and any duplicate tuples this creates.
	// permuteToClauseArguments() has put the arguments in clause order, so the ones
	// to keep are the leading query arguments.
	if(op && nLocalVariables) {
		index8 keptArguments[nArguments];
		for(index8 i = 0; i < nArguments; i++)
			keptArguments[i] = i;
		Operator * projectOperator = CreateProjectOperator(op, nArguments, keptArguments);
		ReleaseOperator(op);
		op = projectOperator;
	}
	return op;
}


/**
 * A compiled service together with its resolved query parameters (signature).
 * One variant is emitted per distinct parameter signature; clauses that
 * resolve to the same signature are combined with a UNION operator.
 */
typedef struct s_CompiledVariant {
	// resolved query parameters, owned by the variant
	TypedTuple * parameters;
	Operator * op;
	// The relation this variant compiles to. Registered before the recursive clauses
	// compile, so that their recursive term has something to dispatch to
	RelationTable const * relation;
	// Temporary operators standing in for the relation while the recursive clauses compile.
	// One variant for each possible parameter IO pattern. Removed by completeRecursiveVariant().
	// See registerTemporaryServices().
	Operator ** recurseOperators;
	size32 nRecurseOperators;
	// whether a recursive clause compiled into this variant
	bool isRecursive;
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


/**
 * Find a compiled variant whose signature matches the given parameters.
 */
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
 * Copy the atom types and parameter IO arrays from the parameters of
 * a compiled variant. Both arrays have the query term arity.
 */
static void getVariantSignature(
	CompiledVariant const * variant, byte atomTypes[], byte parameterIO[])
{
	Atom const * parameters = TypedTuplePeekAtoms(variant->parameters);
	for(index8 i = 0; i < variant->parameters->nAtoms; i++) {
		atomTypes[i] = parameters[i].parameter.atomType;
		parameterIO[i] = parameters[i].parameter.io;
	}
}


/**
 * Find the indices of the input parameters in the parameterIO array
 * and write into the inputArguments array. Returns the number of inputs found.
 */
static size8 findInputArguments(
	byte const parameterIO[], size8 arity, index8 inputArguments[])
{
	size8 nInputs = 0;
	for(index8 i = 0; i < arity; i++) {
		if(parameterIO[i] == PARAMETER_IN)
			inputArguments[nInputs++] = i;
	}
	return nInputs;
}


/**
 * Find the relation registered to the given signature if on exists
 * (if this signature has been compiled before), or else create and register
 * a new relation table.
 */
static RelationTable const * findOrCreateRelation(
	Atom queryTermForm, size8 arity, byte const atomTypes[])
{
	RelationTable const * relation = RelationRegistryFind(queryTermForm, arity, atomTypes);
	if(!relation) {
		relation = CreateRelationTable(0, queryTermForm, arity, atomTypes, 0);
		ASSERT(relation)
		RelationRegistryAdd(relation);
	}
	return relation;
}


/**
 * Test whether two compiled operators have the same index order (order of tuple atoms).
 * This is required to apply the UNION to the two operators. An operator with indexOrder == 0
 * must yield at most one tuple, so that ordering is irrelevant.
 */
static bool sameIndexOrder(Operator const * first, Operator const * second)
{
	if(!first->indexOrder || !second->indexOrder)
		return true;
	return CompareMemory(first->indexOrder, second->indexOrder, first->nArguments) == 0;
}


/**
 * Sort a compiled operator into the identity index order by wrapping a PROJECT() retaining
 * all columns around the operator, unless the operator already is in identity index order.
 * Takes over the caller's reference to the operator; the caller instead obtains a reference
 * to the return Operator (which may or may not be be the same as the given operator).
 */
static Operator * sortOperatorToIndexOrder(Operator * op)
{
	if(!op->indexOrder)
		return op;
	bool ordered = true;
	for(index8 i = 0; i < op->nArguments; i++)
		ordered = ordered && (op->indexOrder[i] == i);
	if(ordered)
		return op;

	index8 argumentMap[op->nArguments];
	for(index8 i = 0; i < op->nArguments; i++)
		argumentMap[i] = i;
	Operator * sortOperator = CreateProjectOperator(op, op->nArguments, argumentMap);
	ReleaseOperator(op);
	return sortOperator;
}


/**
 * Combine two branches of the same signature into their union, taking over the caller's
 * reference to each. Two branches ordered differently are sorted alike first, as a union
 * can only merge relations that agree on the order. Each clause of a rule compiles on its
 * own, and inherits its order from the relations its own terms read, so two branches of
 * one rule have no reason to agree.
 */
static Operator * unionBranches(Operator * first, Operator * second)
{
	if(!sameIndexOrder(first, second)) {
		first = sortOperatorToIndexOrder(first);
		second = sortOperatorToIndexOrder(second);
	}
	Operator * unionOperator = CreateUnionOperator(first, second);
	ReleaseOperator(first);
	ReleaseOperator(second);
	return unionOperator;
}


/**
 * Test whether a clause is recursive with respect to the query. This occurs when the
 * clause contains the a term of the same form as the query term but with the opposite sign,
 * but not necessarily negated. For example, given the query (! even x), the clause
 * (odd x | even x) is recursive since it contains the term (even x).
 */
static bool isRecursiveClauseForm(Atom clauseForm, Atom queryTermForm)
{
	Atom recursiveTermForm = CreateTermForm(
		TermFormGetPredicateForm(queryTermForm),
		!TermFormGetSign(queryTermForm)
	);
	bool recursive = MultisetGetElementMultiple(clauseForm, recursiveTermForm) > 0;
	IFactRelease(recursiveTermForm);
	return recursive;
}


/**
 * Find all clauses (rules) that match the given query term and compile them,
 * producing one or more CompiledVariant. If multiple clauses resolve to the same signature,
 * they are are combined with a UNION operaor. Returns the number of compiled variants.
 *
 * The queryActors tuple must be a series of AT_PARAMETER atoms numbered 1, 2, ...
 * and is not modified; each compiled variant carries its own resolved parameters.
 *
 * The recursive clauses are taken in a second pass, once the non-recursive ones have
 * fixed the parameter types and a service has been registered for their recursive term
 * to dispatch to; see compileQuery().
 *
 * foundRecursiveClause is an output: it is set to true if any matching clause is recursive,
 * and never cleared, so the caller initializes it. That is how compileQuery() knows whether
 * the second pass is needed at all. The second pass sets it again, to no effect.
 */

#define NON_RECURSIVE_PASS	1
#define RECURSIVE_PASS		2

static size8 compileQueryClauses(
	Atom queryTermForm, TypedTuple const * queryActors, int pass,
	bool * foundRecursiveClause, CompiledVariant * variants, size8 nVariants, size8 maxVariants)
{
	size8 queryTermArity = TermFormArity(queryTermForm);
	*foundRecursiveClause = false;

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
 	 * TODO: it might happen that a generated UNION service has the same
	 * signature as an existing service, which becomes part of the UNION.
	 * In this case, the newly generated service should replace the existing one.
	 */

	/**
	 * TODO: here we need the service (multiset >ID element <ID multiple >UINT) where element is input
	 * Since the element role is not a leading column, RelationBTree does not support this.
	 * For now, we simply scan the entire table and filter on matching terms. This is obviously
	 * highly inefficient. A better solution would require multiple indexes on the relation table.
	 */
	Operator const * multisetOperator = GetCoreOperator(SERVICE_MULTISET_ID_ALL);

	Atom multisetQueryTuple[3];
	OperatorContext * multisetContext = OperatorCreateContext(multisetOperator, multisetQueryTuple);
	while(OperatorCall(multisetContext)) {
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

		// If the clause is recursive, we report this to the caller in foundRecursiveClause.
		bool isRecursive = isRecursiveClauseForm(clauseForm, queryTermForm);
		if(isRecursive)
			*foundRecursiveClause = true;
		// Compile only recursive clauses in the RECURSIVE_PASS, and only non-recursive ones
		// in the NON_RECURSIVE_PASS
		if(isRecursive != (pass == RECURSIVE_PASS))
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

						Operator * newService = compileConjunction(
							clauseForm, substClauseActors, matchedTermIndex, queryTermForm,
							queryTermArity, &choices);
						if(!newService)
							continue;
						// Recover the unified parameters from the clause actors
						TypedTupleCopyAt(substClauseActors, matchedTermActorsIndex, resolvedParameters);
						// Check for previously compiled service with the same signature
						CompiledVariant * variant = findVariant(variants, nVariants, resolvedParameters);
						if(variant) {
							// Another clause yielded the same signature: union them
							variant->op = unionBranches(variant->op, newService);
						}
						else {
							ASSERT(nVariants < maxVariants)
							variant = &(variants[nVariants++]);
							SetMemory(variant, sizeof(CompiledVariant), 0);
							variant->parameters = CreateTypedTuple(queryTermArity);
							TypedTupleCopy(resolvedParameters, variant->parameters);
							variant->op = newService;
						}
						// A variant a recursive clause compiled into needs a fixpoint
						// operator to derive it
						variant->isRecursive = variant->isRecursive || isRecursive;
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
	OperatorFreeContext(multisetContext);

	return nVariants;
}


/**
 * Give a variant the relation it compiles to, which a service can then be registered
 * against. Does nothing if the variant has one already.
 */
static void setupVariantRelation(
	CompiledVariant * variant, Atom queryTermForm, size8 arity)
{
	if(variant->relation)
		return;
	byte atomTypes[arity];
	byte parameterIO[arity];
	getVariantSignature(variant, atomTypes, parameterIO);
	variant->relation = findOrCreateRelation(queryTermForm, arity, atomTypes);
}


// Most arguments a query leaves as outputs, for the temporary services of a recursive
// variant. The number of those services is exponential in this; see below.
#define MAX_RECURSIVE_OUTPUT_ARGUMENTS	8


/**
 * Register temporary services for a compiled variant, so that a recursive clause
 * can dispatch its recursive term to (one of) these services. (A recursive term will
 * later dispatch to the relation being compiled, but this does not exist until compilation
 * is finished.)
 *
 * The parameter IO patttern of a recursive term can differ from that of the query term.
 * A join binds the output arguments from its left child to the inputs of its right child,
 * so a recursive term may take as an input an argument that is an output it the query term.
 * 
 * For example, when compiling the query (before x after y) against the recursive clause
 * 
 *   before x after y | ! prec x succ z | ! before z after y
 * 
 * the query pattern will be (before 1>ID after 2>ID) with both x, y as outputs, but the
 * recursive term (before z after y) will have pattern (before 1<ID after 2>ID) since z
 * is produced by (prec x succ z) in a JOIN operation.
 * 
 * Since we cannot tell in advance what the IO pattern of the recursive term might be,
 * we register one service for every possible IO pattern, with the constraint that query inputs
 * are also inputs to the recursive term. Only one or two are used in practice, but which
 * ones is not known until the clauses compile. Each temporary registered service is evaluated by
 * a RECURSE operator. completeRecursiveVariant() removes all temporary services (but not the
 * associated RelationTable) once the recursive clauses have compiled.
 */
static void registerTemporaryServices(
	CompiledVariant * variant, Atom queryTermForm, size8 queryTermArity)
{
	setupVariantRelation(variant, queryTermForm, queryTermArity);

	byte atomTypes[queryTermArity];
	byte queryParameterIO[queryTermArity];
	getVariantSignature(variant, atomTypes, queryParameterIO);

	// Find the output arguments of the query, which a recursive term may bind
	index8 outputArguments[queryTermArity];
	size8 nOutputs = 0;
	for(index8 i = 0; i < queryTermArity; i++) {
		if(queryParameterIO[i] != PARAMETER_IN)
			outputArguments[nOutputs++] = i;
	}
	ASSERT(nOutputs <= MAX_RECURSIVE_OUTPUT_ARGUMENTS)

	// We register one temporary service per subset of the output arguments, covering
	// all possible combinations of input/output arguments, so 2^nOutputs services total.
	variant->nRecurseOperators = ((size32) 1) << nOutputs;
	variant->recurseOperators = Allocate(variant->nRecurseOperators * sizeof(Operator *));
	for(index32 boundOutputs = 0; boundOutputs < variant->nRecurseOperators; boundOutputs++) {
		byte parameterIO[queryTermArity];
		CopyMemory(queryParameterIO, parameterIO, queryTermArity);
		// set the parameter IO of this service to reflect the binary pattern of 
		for(index8 i = 0; i < nOutputs; i++) {
			if(boundOutputs & (((index32) 1) << i))
				parameterIO[outputArguments[i]] = PARAMETER_IN;
		}
		index8 inputArguments[queryTermArity];
		size8 nInputs = findInputArguments(parameterIO, queryTermArity, inputArguments);

		Operator * recurseOperator = CreateRecurseOperator(queryTermArity, inputArguments, nInputs);
		ServiceRegistryAdd(variant->relation, parameterIO, recurseOperator, SERVICE_TEMPORARY);
		variant->recurseOperators[boundOutputs] = recurseOperator;
	}
}


/**
 * Remove the temporary services again, now that the recursive clauses have compiled, and
 * wrap a variant that one of them compiled into in a fixpoint operator. That operator
 * derives the relation, and the recurse operators below it read the tuples it derives.
 */
static void completeRecursiveVariant(CompiledVariant * variant, size8 arity)
{
	if(!variant->recurseOperators)
		return;
	for(index32 i = 0; i < variant->nRecurseOperators; i++) {
		ServiceRegistryRemove(variant->relation, variant->recurseOperators[i]);
		ReleaseOperator(variant->recurseOperators[i]);
	}
	Free(variant->recurseOperators);
	variant->recurseOperators = 0;
	variant->nRecurseOperators = 0;
	if(!variant->isRecursive)
		return;

	byte atomTypes[arity];
	byte parameterIO[arity];
	getVariantSignature(variant, atomTypes, parameterIO);
	index8 inputArguments[arity];
	size8 nInputs = findInputArguments(parameterIO, arity, inputArguments);

	Operator * fixpointOperator = CreateFixpointOperator(
		variant->op, inputArguments, nInputs);
	ReleaseOperator(variant->op);
	variant->op = fixpointOperator;
}


/**
 * Attempt to compile a query into one or more services (variants).
 * Returns the number of variants written to the variants array.
 *
 * Rules (clauses) matching the query are processed in two passes. Non-recursive clauses
 * compile first, and determine the parameter types of the query for each compiled variant;
 * these parameter types must be known before a recursive clause can compile, so that a
 * temporary service can be registers that the recursive term can dispatches to during compilation.
 * A recursive clause therefore must occur together with a non-recursive  clause of the same
 * signature, else it will fail to compile.
 *
 * NOTE: a recursive service is not guaranteed to terminate; see the notes on termination
 * in compiler.md.
 */
static size8 compileQuery(
	Atom queryTermForm, TypedTuple const * queryActors,
	CompiledVariant * variants, size8 maxVariants)
{
	bool foundRecursiveClause;
	// First pass compilation for the non-recursive matching clauses
	size8 nVariants = compileQueryClauses(
		queryTermForm, queryActors, NON_RECURSIVE_PASS, &foundRecursiveClause, variants, 0, maxVariants);
	size8 queryTermArity = TermFormArity(queryTermForm);

	if(foundRecursiveClause) {
		// Second pass for any variant that contained a recursive clause
		for(index8 i = 0; i < nVariants; i++)
			registerTemporaryServices(&variants[i], queryTermForm, queryTermArity);
		nVariants = compileQueryClauses(
			queryTermForm, queryActors, RECURSIVE_PASS, &foundRecursiveClause,
			variants, nVariants, maxVariants);
		for(index8 i = 0; i < nVariants; i++)
			completeRecursiveVariant(&variants[i], queryTermArity);
	}

	// A service is registered against a relation, so every variant needs one. Compiling
	// only needs the relation to exist beforehand when a clause is recursive, which is
	// why registerTemporaryServices() above creates it in that case. Here we cover the rest:
	// every variant when no clause was recursive, and any variant that first appeared
	// while the recursive clauses compiled.
	for(index8 i = 0; i < nVariants; i++)
		setupVariantRelation(&variants[i], queryTermForm, queryTermArity);
	return nVariants;
}


size8 CompileQuery(Formula const * queryTerm, Service services[], size8 maxServices)
{
	ASSERT(IsTermForm(queryTerm->form))
	ASSERT(maxServices > 0)

	// Generalize atoms in the query to parameters
	size8 arity = queryTerm->actors->nAtoms;
	TypedTuple * queryParameters = CreateTypedTuple(arity);
	GetQueryParameters(queryTerm->actors, queryParameters);

	PrintCString("\nCompileQuery()\nqueryParameters: ");
	PrintFormActorsAsFormula(queryTerm->form, queryParameters);
	PrintChar('\n');

	CompiledVariant variants[maxServices];
	size8 nVariants = compileQuery(queryTerm->form, queryParameters, variants, maxServices);

	for(index8 i = 0; i < nVariants; i++) {
		// Parameter types and the relation were resolved by compileQuery()
		byte atomTypes[arity];
		byte parameterIO[arity];
		getVariantSignature(&variants[i], atomTypes, parameterIO);
		services[i] = ServiceRegistryAdd(
			variants[i].relation, parameterIO, variants[i].op, SERVICE_COMPILED);
		ReleaseOperator(variants[i].op);
		FreeTypedTuple(variants[i].parameters);
	}
	FreeTypedTuple(queryParameters);

	PrintCString("-> compiled operators:\n");
	for(index8 i = 0; i < nVariants; i++) {
		PrintService(&services[i]);
		PrintChar('\n');
	}

	return nVariants;
}
