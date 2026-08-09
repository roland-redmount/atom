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
 * which points to an OPERATOR_MACHINE. Unifying the service signature with (3)
 * and renumbering parameters yields the substitution { d -> 3>INT }, and applying
 * this to (3) yields
 * 
 *  + 1<INT - 2<INT = 3>INT                 (5)
 *
 * which becomes the signature of the new service. As we have no more clauses, the
 * found OPERATOR_MACHINE is the final compilation result, and we create a new
 * service mapping (5) to this operator; this service essentally becomes a synonym for
 * (+ 1<INT + 2>INT = 3<INT).
 */

/**
 * Compiling a join: dictionary contains the rule
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
 * A conjunction will always compile to an OPERATOR_JOIN. We initialize the join
 * operator with two terms from (3),
 * 
 *   JOIN(+ 1<INT + 1 = a, + a + 1 = b)         (4)
 * 
 * The JOIN operator will compute sequentially from left to right, To find the left and
 * right child operators of the join, we must dispatch the two terms of (4) separaterly.
 * (If we have > 2 terms we can do a series of joins.) Starting (arbitrarily) with
 * the left term, dispatch matches the service (+ 1<INT + 2<INT = 3>INT) which maps
 * to a OPERATOR_MACHING. After renumbering we obtain the substitution { a -> 2>INT }
 * that we apply to the _left_ term; for the right term, the output parameter 2 must
 * become an input. So that our JOIN operator is now
 * 
 *   JOIN(+ 1<INT + 1 = 2>INT, + 2<INT + 1 = b)       (5)
 * 
 * When later executing this compiled service, we will evaluate the left child operator
 * to obtain values for parameter 2, which will then be copied to input parameter 2 in
 * the right child operator.
 * 
 * (If we would have started with the right term, dispatch would not match the service
 * since the variable a does not match the input parameter 2<INT; in this case we
 * would have to postpone this term.)
 * 
 * Continuing with the right term, dispatch again matches (+ 1<INT + 2<INT = 3>INT)
 * yielding the substitution { b -> 3>INT}, and our JOIN operator becomes
 * 
 *   JOIN(+ 1<INT + 1 = 2>INT, + 2<INT + 1 = 3>INT)     (6)
 *
 * Which is now complete as both child operators have been resolved. 
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
  * Note that y cannot simply be left out of the compiled operators: it is shared
  * between the two terms, so the JOIN operator needs it as an argument in order to
  * constrain one term against the other. We therefore give every such clause-local
  * variable an argument of its own, numbered after the query arguments, and compile
  * the conjunction with this extended arguments tuple. Since the local variables
  * are the trailing arguments, PROJECT need only keep a leading number of arguments.
  *
  * The PROJECT operation requires checking for duplicate tuples (unless the kept
  * arguments are known to be a unique key for the relation). This is problematic
  * since we want the operator to yield one tuple at a time; currently PROJECT
  * enumerates its entire child relation when its context is created.
  * To enable efficient duplicate removal, the child operator should yield tuples in
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
  * JOIN operator we will need two resolved terms. The first term (+ m + 1 = n)
  * matches service (+ 1>INT + 2<INT = 3<INT) and we obtain
  *
  *   JOIN(+ 2>INT + 1 = $1>INT, ...)
  *
  * the second term is then (integer 2<INT factorial e). We cannot match this to
  * the current service however, since we do not yet know the type of the argument e.
  * TODO: this will likely require a separate OPERATOR_RECURSE or similar that is
  * treated specially by the compiler.
  */


 /**
  * The shape of a recursive operator tree. A recursive rule gives an operator
  * tree that is a graph rather than a tree: some leaf of it refers back to the
  * operator at its root. The compiled (integer n factorial f) would be
  *
  *   UNION(base clause, PROJECT(JOIN(..., JOIN(RECURSE, ...))))
  *
  * Note that the cycle is in the plan, not in any one evaluation of it: the
  * recursion terminates on the values, as in any recursive procedure.
  *
  * What blocks this today is not the cycle but OperatorCreateContext(), which
  * builds the whole context tree eagerly: a permute context creates its child
  * context, a join context creates its left context and calls it, a union
  * context creates both and takes a lookahead tuple, and a project context
  * drains its entire child relation. On a cyclic graph that recursion never
  * bottoms out, and it does so at construction time, where there are no values
  * to terminate on.
  *
  * Creating each child context lazily, on the first call that needs a tuple
  * from it, fixes exactly that. Contexts then become what stack frames are to
  * a recursive procedure: one per active invocation, created as the recursion
  * descends and freed as it unwinds. Nothing else about the split between
  * operators and contexts needs to change, as an operator is immutable once
  * constructed and all evaluation state already lives in the context. Contexts
  * are already per-invocation rather than per-operator: a join operator creates
  * a fresh right context for every left tuple, while its left context is live.
  *
  * The back edge should be an operator of its own rather than a pointer from
  * some existing operator back to the root, so that every traversal
  * (PrintOperator(), teardown, any later optimization) can see the cycle
  * instead of following it forever. Its reference to the root must not be
  * counted, or the reference count of a recursive service could never reach
  * zero; the service registry holds the one owning reference, and removing the
  * service tears the whole cycle down. The pointer is only known once
  * compilation completes, so it is back-patched at the end.
  *
  * Giving a context a pointer to its parent context would also be worth having:
  * it gives a recursion depth for a guard against runaway recursion, and
  * something to print when a query misbehaves.
  */


 /**
  * Terminating a recursive service. Two problems remain once the tree above can
  * be evaluated at all, and neither is solved by the operator alone.
  *
  * The first is evaluation order. A UNION operator merges two sorted children
  * and so must hold a lookahead tuple from each, which means it enters the
  * recursive branch even when the base clause alone would answer the query.
  * That is the opposite of Prolog, where clause order lets the base case answer
  * without the recursive clause ever being tried. A recursive union may
  * therefore want to be an ordered concatenation of its children instead, with
  * duplicate removal left to an enclosing PROJECT. This also matters because
  * the sortedness a UNION assumes of its children is not something a recursive
  * branch obviously provides.
  *
  * The second is that nothing guarantees termination. For the factorial rule
  * above, the fact (integer 0 factorial 1) is not by itself enough: evaluating
  * the query for n = 0 finds the fact, but the recursive clause is also tried,
  * computing m = -1 and descending forever. The recursive clause needs a
  * guard -- the precondition noted above -- or an evaluation order that answers
  * from the base clause first.
  *
  * Rules over a finite stored relation are a different case. With
  *
  *   before x after y <- prec x succ y
  *   before x after y <- prec x succ z & before z after y
  *
  * the answer is a fixpoint: start from the base relation and apply the rule
  * body to the tuples found so far until no new tuple appears. That terminates
  * without any guard, and is what a bottom-up OPERATOR_FIXPOINT would compute,
  * whereas the factorial rule wants the top-down evaluation described above,
  * as its domain is infinite and only one argument value is of interest.
  * We will likely want both, with memoization of computed tuples as the bridge:
  * PROJECT already materializes its child into a B-tree, and a memo keyed by
  * the bound input arguments is a small step from that.
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

	// Find a term that can be compiled.
	// First iterate over term forms in the clause form
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
 * query-matched term. The new parametesr are numbered consecutively after the query parameters.
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

	ClauseCompileState clauseState = {
		.form = clauseForm,
		.actors = clauseActors,
		.termActorsIndices = termActorsIndices,
		.nTerms = clauseNTerms,
		.nArguments = nArguments + nLocalVariables,
		.matchedTermIndex = matchedTermIndex,
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
 * A compiled service together with the query parameters it resolved to.
 * One variant is emitted per distinct parameter signature; clauses that
 * resolve to the same signature are combined into a UNION, as before.
 */
typedef struct s_CompiledVariant {
	// resolved query parameters, owned by the variant
	TypedTuple * parameters;
	Operator * op;
	// The relation this variant compiles to, registered before the recursive clauses
	// compile so that their recursive term has something to dispatch to
	RelationTable const * relation;
	// The operator standing in for the relation while the recursive clauses compile,
	// removed again by completeRecursiveVariant(); see compileService()
	Operator * recurseOperator;
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
 * Recover the atom types and parameter IO of a compiled variant from the query
 * parameters its clauses resolved to. Both arrays have the query term arity.
 */
static void findVariantSignature(
	CompiledVariant const * variant, byte atomTypes[], byte parameterIO[])
{
	Atom const * parameters = TypedTuplePeekAtoms(variant->parameters);
	for(index8 i = 0; i < variant->parameters->nAtoms; i++) {
		atomTypes[i] = parameters[i].parameter.atomType;
		parameterIO[i] = parameters[i].parameter.io;
	}
}


/**
 * Collect the indices of the arguments a service takes as inputs, which are the ones a
 * caller of the compiled relation binds. Returns the number of inputs found.
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
 * The relation a compiled variant provides, reusing the relation table if this signature
 * has been compiled before.
 *
 * The relation is registered under its predicate form, and not under the term form of the
 * query. Dispatch looks a relation up by predicate form, so a relation registered under a
 * term form is invisible to it, and the recursive term of a recursive clause would then
 * find no service to dispatch to.
 */
static RelationTable const * findVariantRelation(
	Atom queryTermForm, size8 arity, byte const atomTypes[])
{
	Atom predicateForm = TermFormGetPredicateForm(queryTermForm);
	RelationTable const * relation = RelationRegistryFind(predicateForm, arity, atomTypes);
	if(!relation) {
		relation = CreateRelationTable(0, predicateForm, arity, atomTypes, 0);
		ASSERT(relation)
		RelationRegistryAdd(relation);
	}
	return relation;
}


/**
 * Test whether two compiled branches yield their tuples in the same order, as merging
 * them into a union requires. A branch declaring no order yields at most one tuple, and
 * so is ordered alike with any other.
 */
static bool sameIndexOrder(Operator const * first, Operator const * second)
{
	if(!first->indexOrder || !second->indexOrder)
		return true;
	return CompareMemory(first->indexOrder, second->indexOrder, first->nArguments) == 0;
}


/**
 * Sort a compiled branch into the identity index order, unless it is in that order
 * already. Takes over the caller's reference to the operator.
 *
 * A projection keeping every argument drops nothing and materializes its child, which is
 * what sorts it; see CreateProjectOperator().
 */
static Operator * sortBranchToIndexOrder(Operator * op)
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
		first = sortBranchToIndexOrder(first);
		second = sortBranchToIndexOrder(second);
	}
	Operator * unionOperator = CreateUnionOperator(first, second);
	ReleaseOperator(first);
	ReleaseOperator(second);
	return unionOperator;
}


/**
 * Test whether a clause is recursive with respect to the query, which it is when the
 * clause contains the query term with the opposite sign. Resolving the query against
 * such a clause leaves a term asking for the very relation being compiled.
 *
 * NOTE: what marks the recursion is the opposite sign, not a negative one. In atom a fact
 * may be a negated term, and so may a query. The rule (odd x | even x) can be read as the
 * implication (odd x -> ! even x) just as well as (even x -> ! odd x), so the query
 * (! even x) recurses through the positive (even x) term of that rule.
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
 * Compile the clauses the query resolves against whose recursiveness matches this pass,
 * adding the operator of each to the variant of its parameter signature. Clauses
 * resolving to the same signature are combined into a UNION. Returns the resulting
 * number of variants.
 *
 * The queryActors tuple must be a series of AT_PARAMETER atoms numbered 1, 2, ...
 * and is not modified; each compiled variant carries its own resolved parameters.
 *
 * The recursive clauses are taken in a second pass, once the non-recursive ones have
 * fixed the parameter types and a service has been registered for their recursive term
 * to dispatch to; see compileService().
 *
 * The first pass sets foundRecursiveClause if it comes across any recursive clause, which
 * is how compileService() knows whether to make the second pass at all. The second pass
 * passes 0 here, as the answer is known by then.
 */
static size8 compileClauses(
	Atom queryTermForm, TypedTuple const * queryActors, bool recursivePass,
	bool * foundRecursiveClause,
	CompiledVariant * variants, size8 nVariants, size8 maxVariants)
{
	size8 queryTermArity = TermFormArity(queryTermForm);

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
		// Take the recursive clauses in the second pass, once there is a service for
		// their recursive term to dispatch to
		bool recursive = isRecursiveClauseForm(clauseForm, queryTermForm);
		if(recursive && foundRecursiveClause)
			*foundRecursiveClause = true;
		if(recursive != recursivePass)
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
							clauseForm, substClauseActors, matchedTermIndex, queryTermArity, &choices);
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
						variant->isRecursive = variant->isRecursive || recursive;
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
	findVariantSignature(variant, atomTypes, parameterIO);
	variant->relation = findVariantRelation(queryTermForm, arity, atomTypes);
}


/**
 * Register a temporary service for a variant, so that the recursive clauses have
 * something to dispatch their recursive term to. That term asks for the relation being
 * compiled, whose real service does not exist until compilation finishes.
 *
 * The temporary service is evaluated by a recurse operator, and completeRecursiveVariant()
 * removes it again once the recursive clauses have compiled. Only the service is
 * temporary: the relation it is registered against is the one the finished service
 * provides.
 */
static void setupRecursiveVariant(
	CompiledVariant * variant, Atom queryTermForm, size8 arity)
{
	setupVariantRelation(variant, queryTermForm, arity);

	byte atomTypes[arity];
	byte parameterIO[arity];
	findVariantSignature(variant, atomTypes, parameterIO);
	index8 inputArguments[arity];
	size8 nInputs = findInputArguments(parameterIO, arity, inputArguments);

	variant->recurseOperator = CreateRecurseOperator(arity, inputArguments, nInputs);
	ServiceRegistryAdd(variant->relation, parameterIO, variant->recurseOperator);
}


/**
 * Remove the temporary service again, now that the recursive clauses have compiled, and
 * wrap a variant that one of them compiled into in a fixpoint operator. That operator
 * derives the relation, and the recurse operators below it read the tuples it derives.
 */
static void completeRecursiveVariant(CompiledVariant * variant, size8 arity)
{
	if(!variant->recurseOperator)
		return;
	ServiceRegistryRemove(variant->relation, variant->recurseOperator);
	ReleaseOperator(variant->recurseOperator);
	variant->recurseOperator = 0;
	if(!variant->isRecursive)
		return;

	byte atomTypes[arity];
	byte parameterIO[arity];
	findVariantSignature(variant, atomTypes, parameterIO);
	index8 inputArguments[arity];
	size8 nInputs = findInputArguments(parameterIO, arity, inputArguments);

	Operator * fixpointOperator = CreateFixpointOperator(
		variant->op, inputArguments, nInputs);
	ReleaseOperator(variant->op);
	variant->op = fixpointOperator;
}


/**
 * Attempt to compile services with the given form and actors.
 * Returns the number of variants written to the variants array.
 *
 * The clauses are taken in two passes. The non-recursive ones compile first, and fix the
 * parameter types of each variant. Those types have to be settled before a recursive
 * clause can compile, because the service its recursive term dispatches to is registered
 * with them. A recursive clause therefore has to occur together with a non-recursive
 * clause of the same signature; on its own it simply fails to compile, which the choice
 * point machinery already tolerates.
 *
 * NOTE: a recursive service is not guaranteed to terminate; see the notes on terminating
 * a recursive service at the top of this file.
 */
static size8 compileService(
	Atom queryTermForm, TypedTuple const * queryActors,
	CompiledVariant * variants, size8 maxVariants)
{
	bool foundRecursiveClause = false;
	size8 nVariants = compileClauses(
		queryTermForm, queryActors, false, &foundRecursiveClause, variants, 0, maxVariants);
	size8 arity = TermFormArity(queryTermForm);

	if(foundRecursiveClause) {
		for(index8 i = 0; i < nVariants; i++)
			setupRecursiveVariant(&variants[i], queryTermForm, arity);
		nVariants = compileClauses(
			queryTermForm, queryActors, true, 0, variants, nVariants, maxVariants);
		for(index8 i = 0; i < nVariants; i++)
			completeRecursiveVariant(&variants[i], arity);
	}

	// A service is registered against a relation, so every variant needs one. Compiling
	// only needs the relation to exist beforehand when a clause is recursive, which is
	// why setupRecursiveVariant() above creates it in that case. Here we cover the rest:
	// every variant when no clause was recursive, and any variant that first appeared
	// while the recursive clauses compiled.
	for(index8 i = 0; i < nVariants; i++)
		setupVariantRelation(&variants[i], queryTermForm, arity);
	return nVariants;
}


size8 CompileService(Formula const * queryTerm, Service services[], size8 maxServices)
{
	ASSERT(IsTermForm(queryTerm->form))
	ASSERT(maxServices > 0)

	// Generalize atoms in the query to parameters
	size8 arity = queryTerm->actors->nAtoms;
	TypedTuple * queryParameters = CreateTypedTuple(arity);
	actorsToParameters(queryTerm->actors, queryParameters);

	PrintCString("\nCompileService()\nqueryParameters: ");
	PrintFormActorsAsFormula(queryTerm->form, queryParameters);
	PrintChar('\n');

	CompiledVariant variants[maxServices];
	size8 nVariants = compileService(queryTerm->form, queryParameters, variants, maxServices);

	for(index8 i = 0; i < nVariants; i++) {
		// Parameter types and the relation were resolved by compileService()
		byte atomTypes[arity];
		byte parameterIO[arity];
		findVariantSignature(&variants[i], atomTypes, parameterIO);
		services[i] = ServiceRegistryAdd(variants[i].relation, parameterIO, variants[i].op);
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
