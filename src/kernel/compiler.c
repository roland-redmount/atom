#include "kernel/compiler.h"
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/ClauseForm.h"
#include "lang/formula.h"
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
 *
 * Building with DEBUG_COMPILER defined makes the compiler trace each query it compiles,
 * the rules it resolves against and the services it emits. The trace is off by default,
 * since AssertFact() compiles a query for every fact it is given; see cmake option
 * DEBUG_COMPILER in CMakeLists.txt.
 */


/**
 * A query term with untyped output parameters may dispatch to multiple services.
 * Each term dispatched during compilation creates a choice point, and each combination of
 * such choices yields a compiled service with a specific signature. Typically, many choice
 * points will have only one choice.
 *
 * We re-run the whole compilation once per combination, forcing a different choice each time.
 * Each choice taken at a choice point is identified by the type signature of the chosen service.
 * A re-run asks dispatch for a new matching service, besides those found so far;
 * see DispatchParameterizedQuery().
 *
 * NOTE: a choice point is identified by the position at which it is encountered. That is
 * well defined because the terms of a clause are compiled in a deterministic order.
 */

// Most choice points one compilation may reach, which is one per term dispatched
#define MAX_CHOICE_POINTS	8

// Most alternatives one choice point may enumerate
#define MAX_CHOICE_POINT_MATCHES	8

/**
 * One dispatched term, and the choices made for it so far.
 */
typedef struct s_ChoicePoint {
	// Type signature of the service each choice dispatched to. These are the signatures
	// the next call to DispatchParameterizedQuery() excludes, so that it takes a match
	// this choice point has not taken yet.
	TypeSignature choiceSignatures[MAX_CHOICE_POINT_MATCHES];
	size8 nChoices;
	// whether a match outside choiceSignatures exists
	bool hasNextMatch;
#ifdef DEBUG
	// The form of the term dispatched here, kept to verify that every run reaches this
	// choice point with the same term
	Atom termForm;
#endif
} ChoicePoint;

/**
 * The choice points of one compilation, which is the path the current run takes through
 * the tree of combinations: one level per term dispatched, in the order the terms compile.
 * A run walks the path from the root, so the choice points beyond its depth are the ones
 * it has yet to reach.
 */
typedef struct s_ChoiceTree {
	ChoicePoint choicePoints[MAX_CHOICE_POINTS];
	// number of choice points the current run has reached
	index8 depth;
} ChoiceTree;


static void resetChoiceTree(ChoiceTree * choiceTree)
{
	SetMemory(choiceTree, sizeof(ChoiceTree), 0);
}


/**
 * Advance to the deepest choice point that still has a next (untried) match, and reset the
 * choice points below it. Returns false when no choice point has a next match.
 */
static bool nextChoiceBranch(ChoiceTree * choiceTree)
{
	for(index8 i = choiceTree->depth; i > 0; i--) {
		index8 d = i - 1;
		if(choiceTree->choicePoints[d].hasNextMatch) {
			// The choices made at this choice point are kept, so that the next run takes a
			// match outside them; the choice points below it start afresh
			for(index8 j = i; j < MAX_CHOICE_POINTS; j++) {
				choiceTree->choicePoints[j].nChoices = 0;
				choiceTree->choicePoints[j].hasNextMatch = false;
			}
			choiceTree->depth = 0;
			return true;
		}
	}
	return false;
}


/**
 * Generalize the actors of a term to its signature, which is what dispatch matches: a
 * parameter stands for itself, and a constant for an input parameter of the constant's own
 * type, which is all a constant asks of a service. The number given to a constant is above
 * every number the term uses, so that it constrains nothing to be equal to it.
 *
 * The parameters array must hold as many atoms as the term has actors. Actors of a term
 * are parameters and constants only, every variable of the clause having been given a
 * parameter by parameterizeLocalVariables().
 */
static void getTermParameters(TypedTuple const * termActors, Atom parameters[])
{
	uint8 nextNumber = 1;
	for(index8 i = 0; i < termActors->nAtoms; i++) {
		TypedAtom actor = TypedTupleGetElement(termActors, i);
		ASSERT(actor.type != AT_VARIABLE)
		if((actor.type == AT_PARAMETER) && (actor.atom.parameter.number >= nextNumber))
			nextNumber = actor.atom.parameter.number + 1;
	}
	for(index8 i = 0; i < termActors->nAtoms; i++) {
		TypedAtom actor = TypedTupleGetElement(termActors, i);
		if(actor.type == AT_PARAMETER)
			parameters[i] = actor.atom;
		else {
			// a parameter number is a uint8, which a term arity cannot exhaust
			ASSERT(nextNumber < 255)
			parameters[i] = (Atom) {
				.parameter = {
					.number = nextNumber++, .io = PARAMETER_IN, .atomType = actor.type}
			};
		}
	}
}


/**
 * What dispatchTermService() may do when the service registry holds no service for a term.
 * Compiling a term from the rules succeeds whatever the term binds, so a clause only offers
 * that to a term once none of its terms dispatches to a registered service; see
 * compileConjunctionRecursive().
 */
#define COMPILE_FALLBACK_NONE	1
#define COMPILE_FALLBACK_RULES	2

static size8 compileParameterizedQuery(
	Atom queryTermForm, TypedTuple const * queryParameters,
	Service services[], size8 maxServices);


/**
 * Number the parameters of a term by position, which is what a parameterized query is:
 * every position a parameter of its own, so that a parameter occurring twice in the term
 * loses its equality constraint. That constraint is not lost from the compiled term, which
 * applies it above the service; see constrainRepeatedArguments(). This is what
 * DispatchQuery() does to the actors it is given, done here to the parameters of a term.
 */
static void setupParameterizedQuery(
	Atom const termParameters[], size8 termArity, TypedTuple * queryParameters)
{
	for(index8 i = 0; i < termArity; i++) {
		Atom parameter = termParameters[i];
		parameter.parameter.number = i + 1;
		TypedTupleSetElement(queryParameters, i, CreateTypedAtom(AT_PARAMETER, parameter));
	}
}


/**
 * Find the service a term is to be compiled against: the one the service registry holds,
 * or, where the caller permits it, one compiled from the rules answering the term. This is
 * what lets a rule body term be answered by another rule, and is the same two steps
 * UserQuery() takes for a query the user asks.
 *
 * The compiled services are registered, so the second dispatch finds whichever of them this
 * term asks for. Dispatching again rather than picking from what the compilation returned
 * keeps permutation matching and the parameter type checks in one place.
 *
 * The exclusion list, the resolved types and hasNextMatch are as for
 * DispatchParameterizedQuery(). Returns true if a service was found.
 */
static bool dispatchTermService(
	Atom termForm, Atom const termParameters[], size8 termArity, int fallback,
	Service * service, index8 permutation[],
	TypeSignature const excludedSignatures[], size8 nExcluded, bool * hasNextMatch)
{
	if(DispatchParameterizedQuery(
		termForm, termParameters, termArity, service, permutation,
		excludedSignatures, nExcluded, hasNextMatch))
		return true;
	if(fallback != COMPILE_FALLBACK_RULES)
		return false;

	TypedTuple * queryParameters = CreateTypedTuple(termArity);
	setupParameterizedQuery(termParameters, termArity, queryParameters);
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = compileParameterizedQuery(
		termForm, queryParameters, services, MAX_COMPILED_SERVICES);
	FreeTypedTuple(queryParameters);
	if(!nServices)
		return false;

	return DispatchParameterizedQuery(
		termForm, termParameters, termArity, service, permutation,
		excludedSignatures, nExcluded, hasNextMatch);
}


/**
 * Dispatch a term, adding a choice point for it
 */
static bool dispatchAtNewChoicePoint(
	Atom termForm, TypedTuple const * termActors, int fallback, Service * service,
	index8 permutation[], ChoiceTree * choiceTree)
{
	// Add a new choice point
	ASSERT(choiceTree->depth < MAX_CHOICE_POINTS)
	ChoicePoint * choicePoint = &(choiceTree->choicePoints[choiceTree->depth++]);
	// dispatch term, parameterized
	size8 termArity = termActors->nAtoms;
	Atom termParameters[termArity];
	getTermParameters(termActors, termParameters);
	ASSERT(choicePoint->nChoices < MAX_CHOICE_POINT_MATCHES)
#ifdef DEBUG
	if(choicePoint->nChoices)
		ASSERT(SameAtoms(choicePoint->termForm, termForm))
	choicePoint->termForm = termForm;
#endif
	if(!dispatchTermService(
		termForm, termParameters, termArity, fallback, service, permutation,
		choicePoint->choiceSignatures, choicePoint->nChoices,
		&(choicePoint->hasNextMatch)))
		return false;

	// The relation the match reads is what names it, so recording its signature is what
	// makes a later run take a different match here
	choicePoint->choiceSignatures[choicePoint->nChoices] = service->relation->typeSignature;
	choicePoint->nChoices++;
	return true;
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
 * Build the operator of a term from the service answering it, taking only the arguments of
 * the term itself. Any non-parameter actor is a constant restricting one argument of that
 * service, and is bound by a PERMUTE operator wrapped around it; a term without constants
 * compiles to the service operator directly. The permutation needs no operator of its own:
 * it is carried by the clauseMap. (Variables occurring in the clause but not in the query
 * are given parameter numbers of their own by parameterizeLocalVariables() before we get
 * here.)
 *
 * The permutation array maps each service parameter to the term actor providing it, as
 * dispatch reports it; a service built for the term itself takes the identity.
 *
 * The clauseMap array is set to the clause argument provided by each argument of the
 * compiled operator, and so has length equal to its nArguments. The caller places
 * those arguments into the clause arguments tuple, either as a child of a JOIN
 * operator or, for a single term, with a PERMUTE operator.
 *
 * The serviceParameters tuple is set to the service's parameters, permuted to match the
 * term actors order.
 *
 * The caller keeps its own reference to the service operator.
 */
static Operator * compileTermService(
	Service const * termService, TypedTuple const * termActors, index8 const permutation[],
	TypedTuple * serviceParameters, index8 clauseMap[])
{
	size8 termArity = termActors->nAtoms;

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
						.atomType = termService->relation->typeSignature.atomTypes[i],
						.io = termService->parameterIO[i]
					}
				}
			)
		);
	}
	ASSERT(nMapped == nArguments)

	Operator * op;
	if(!nConstants) {
		// Without constants to bind, the service operator is used as it is
		AcquireOperator(termService->op);
		op = termService->op;
	}
	else {
		op = CreatePermuteOperator(
			nArguments, constants, constantTypes, nConstants, argumentMap,
			termService->op);
	}
	// A variable occurring more than once in the term constrains the arguments
	// providing it to be equal
	return constrainRepeatedArguments(op, clauseMap);
}


/**
 * A term compiles to the service that dispatch matches to it; see compileTermService().
 * The fallback says whether a term the service registry cannot answer may be compiled from
 * the rules; see dispatchTermService().
 */
static Operator * compileTerm(
	Atom termForm, TypedTuple const * termActors, int fallback,
	TypedTuple * serviceParameters, index8 clauseMap[], ChoiceTree * choiceTree)
{
	// attempt to locate a service for the term
	size8 termArity = termActors->nAtoms;
	index8 permutation[termArity];
	Service termService;
	if(!dispatchAtNewChoicePoint(
		termForm, termActors, fallback, &termService, permutation, choiceTree))
		return 0;

	return compileTermService(
		&termService, termActors, permutation, serviceParameters, clauseMap);
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
 * A compiled service together with its resolved query parameters (signature).
 * One variant is emitted per distinct parameter signature; clauses that
 * resolve to the same signature are combined with a UNION operator.
 */
typedef struct s_CompiledVariant {
	// resolved query parameters, owned by the variant
	TypedTuple * parameters;
	Operator * op;
	// The relation this variant compiles to, and a reference to it. Created before the
	// recursive clauses compile, as their recursive term reads it.
	Relation const * relation;
	// whether a recursive clause compiled into this variant
	bool isRecursive;
} CompiledVariant;


/**
 * The type signature of a compiled variant, from the parameters resolved for it.
 */
static TypeSignature getVariantTypeSignature(CompiledVariant const * variant)
{
	size8 arity = variant->parameters->nAtoms;
	byte atomTypes[arity];
	Atom const * parameters = TypedTuplePeekAtoms(variant->parameters);
	for(index8 i = 0; i < arity; i++)
		atomTypes[i] = parameters[i].parameter.atomType;
	return CreateTypeSignature(atomTypes, arity);
}


/**
 * Copy the parameter IO of a compiled variant, from the parameters resolved for it, to an
 * array of the query term arity.
 */
static void getVariantParameterIO(CompiledVariant const * variant, byte parameterIO[])
{
	Atom const * parameters = TypedTuplePeekAtoms(variant->parameters);
	for(index8 i = 0; i < variant->parameters->nAtoms; i++)
		parameterIO[i] = parameters[i].parameter.io;
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
	// Choice points taken during the compilation
	ChoiceTree * choiceTree;
	// The variant a recursive term of this clause reads, or 0 when there is none to read,
	// which is every clause of the non-recursive pass. A recursive term reads the relation
	// of this variant and resolves its outputs to that relation's column types, and has to
	// bind at least the arguments the variant takes as inputs; see compileRecursiveTerm().
	CompiledVariant const * recursiveVariant;
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
 * A recursive term reads the relation the compilation is deriving, which no service
 * answers yet, so it compiles to a RECURSE operator built for the term rather than to a
 * service dispatch finds. The operator enumerates the tuples derived so far by the FIXPOINT
 * operator that completeRecursiveVariant() puts above the clause.
 *
 * The term is taken last of the clause, so every argument another term provides is already
 * an input by then and the rest are outputs: the parameter IO of the term is settled, and
 * so is the operator. The outputs resolve to the column types of the relation, which is
 * where a recursive term gets its types from, the non-recursive clauses having fixed them.
 *
 * Returns 0 where the term cannot read the relation: when a typed parameter disagrees with
 * the column it reads, and when the term leaves an argument free that the query binds. The
 * derivation is keyed on what the query binds, so a term asking for less has no call
 * binding to name it; assertCallBindingIsNamed() in operator.c is the same condition where
 * the operators meet. See testCompileRecursiveTermUnboundInput().
 */
static Operator * compileRecursiveTerm(
	ClauseCompileState const * clauseState, TypedTuple const * termActors,
	TypedTuple * serviceParameters, index8 clauseMap[])
{
	ASSERT(clauseState->recursiveVariant)
	Relation const * relation = clauseState->recursiveVariant->relation;
	size8 termArity = termActors->nAtoms;
	ASSERT(termArity == relation->nColumns)
	Atom termParameters[termArity];
	getTermParameters(termActors, termParameters);
	// The arguments the query binds, which this term has to bind too
	byte queryParameterIO[termArity];
	getVariantParameterIO(clauseState->recursiveVariant, queryParameterIO);

	byte const * atomTypes = relation->typeSignature.atomTypes;
	byte parameterIO[termArity];
	for(index8 i = 0; i < termArity; i++) {
		// A parameter of a known type must be the type of the column it reads
		if(termParameters[i].parameter.atomType
			&& (termParameters[i].parameter.atomType != atomTypes[i]))
			return 0;
		parameterIO[i] = termParameters[i].parameter.io;
		if((queryParameterIO[i] == PARAMETER_IN) && (parameterIO[i] != PARAMETER_IN))
			return 0;
	}

	index8 inputArguments[termArity];
	size8 nInputs = findInputArguments(parameterIO, termArity, inputArguments);
	Operator * recurseOperator = CreateRecurseOperator(termArity, inputArguments, nInputs);

	// The term reads the relation directly, so its arguments are the relation columns in
	// order and no permutation is involved
	index8 permutation[termArity];
	for(index8 i = 0; i < termArity; i++)
		permutation[i] = i;
	Service recurseService = {
		.relation = relation,
		.op = recurseOperator,
		.kind = SERVICE_TEMPORARY
	};
	CopyMemory(parameterIO, recurseService.parameterIO, termArity);
	Operator * op = compileTermService(
		&recurseService, termActors, permutation, serviceParameters, clauseMap);
	// compileTermService() took its own reference to the operator
	ReleaseOperator(recurseOperator);
	return op;
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
	 * Find a term that can be compiled, in three passes over the term forms of the clause.
	 *
	 * A term only dispatches to a registered service once its own inputs are available,
	 * which is what makes taking the first term that compiles sound. Two kinds of term
	 * compile whatever is bound and so must not be taken while another term still can.
	 *
	 * The first is a term compiled from the rules, which yields a plan for any binding
	 * pattern the term is asked in. Taken early it would read a relation unbound where a
	 * later binding would have restricted it. The first pass therefore offers no term to
	 * the rules, and the second offers them to every term in turn.
	 *
	 * The second is the recursive term, which reads the relation being derived and so
	 * compiles whatever is bound. It would win over a term whose outputs it should be
	 * consuming, leaving that term to read its relation unbound. It is also the term with
	 * nothing to contribute: its parameter types are already settled by the non-recursive
	 * clauses, and a clause whose other terms do not compile yields no fixpoint anyway. So
	 * it is taken last of all, in the third pass, and skipped by the two before it. Taken
	 * last, every argument another term provides is an input by then, which is what settles
	 * its operator; see compileRecursiveTerm().
	 */
	for(index8 pass = 0; !op && (pass < 3); pass++) {
		// Only the second pass offers a term to the rules. The recursive term of the third
		// pass compiles against the relation being derived, and offering it to the rules
		// would re-enter the very compilation it belongs to.
		int fallback = (pass == 1) ? COMPILE_FALLBACK_RULES : COMPILE_FALLBACK_NONE;
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
			// A term of the query's own form is the recursive one, which only the third
			// pass takes and only the third pass considers
			bool isRecursiveTerm = SameAtoms(negatedTermForm, clauseState->queryTermForm);
			if((isRecursiveTerm == (pass < 2)) || (isRecursiveTerm && !clauseState->recursiveVariant)) {
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
#ifdef DEBUG_COMPILER
				PrintCString("Term: ");
				PrintFormActorsAsFormula(negatedTermForm, termActors);
				PrintChar('\n');
#endif
				// Attempt to compile this term. A recursive term reads the relation
				// being derived and builds its own operator; every other term dispatches,
				// adding a choice point.
				op = isRecursiveTerm
					? compileRecursiveTerm(
						clauseState, termActors, serviceParameters, termClauseMap)
					: compileTerm(
						negatedTermForm, termActors, fallback, serviceParameters,
						termClauseMap, clauseState->choiceTree);
#ifdef DEBUG_COMPILER
				PrintCString("serviceParameters = ");
				TypedTuplePrint(serviceParameters);
				PrintChar('\n');
#endif

				if(op) {
					clauseState->termExcluded[termIndex] = true;
					nTermsExcluded++;
					propagateTermParameterTypes(clauseState, termIndex, termActors, serviceParameters);
#ifdef DEBUG_COMPILER
					PrintCString("Updated clause: ");
					PrintFormActorsAsFormula(clauseState->form, clauseState->actors);
					PrintChar('\n');
#endif
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
#ifdef DEBUG_COMPILER
			PrintCString("Clause does not provide every argument\n");
#endif
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
 *
 * A recursive clause is compiled against the relation of the variant being derived, which
 * is what its recursive term reads; recursiveVariant is 0 in the non-recursive pass, where
 * there is no variant to recurse into and a recursive term therefore does not compile.
 */
static Operator * compileConjunction(
	Atom clauseForm, TypedTuple * clauseActors, index8 matchedTermIndex, Atom queryTermForm,
	size8 nArguments, ChoiceTree * choiceTree, CompiledVariant const * recursiveVariant)
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
		.choiceTree = choiceTree,
		.recursiveVariant = recursiveVariant
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
 * Union two Operatrors having the same signature, taking over the caller's
 * reference to each. If the operators have different indexOrder, they are sorted first
 * (UNION can only merge relations that agree on the order. Each clause of a rule compiles on its
 * own, and inherits its order from the relations its own terms read, so two branches of
 * one rule have no reason to agree.
 */
static Operator * unionOperators(Operator * first, Operator * second)
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
 * The recursive clauses are compiled in a second pass, once the non-recursive ones have
 * fixed the parameter types and a service has been registered for their recursive term
 * to dispatch to; see compileQueryVariants().
 *
 * foundRecursiveClause is set to true if any matched clause was recursive.
 * foundRecursiveClause is never cleared, so the caller must initializes it (??)
 * that is how compileQueryVariants() knows whether the second pass is needed at all.
 * The second pass sets it again, to no effect. TODO: this is confusing, revise
 */

#define NON_RECURSIVE_PASS	1
#define RECURSIVE_PASS		2

static size8 compileQueryClauses(
	Atom queryTermForm, TypedTuple const * queryActors, int pass,
	CompiledVariant * recursiveVariant, bool * foundRecursiveClause,
	CompiledVariant * variants, size8 nVariants, size8 maxVariants)
{
	size8 queryTermArity = TermFormArity(queryTermForm);
	*foundRecursiveClause = false;

	// A recursive clause is compiled against one variant, which its recursive term reads,
	// so the recursive pass has a variant and the non-recursive pass has none
	ASSERT((pass == RECURSIVE_PASS) == (recursiveVariant != 0))

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
	 * TODO: here we need the service (multiset >ID element <ID multiple >INT) where element is input
	 * Since the element role is not a leading column, RelationBTree does not support this.
	 * For now, we simply scan the entire table and filter on matching terms. This is obviously
	 * highly inefficient. A better solution would require multiple indexes on the relation table.
	 * NOTE: once FILTER operator is in place we can register a compiled service for this at bootstrap time.
	 */
	Operator const * multisetOperator = GetCoreOperator(SERVICE_MULTISET_ID_ALL);

	Atom multisetQueryTuple[3];
	OperatorContext * multisetContext = OperatorCreateContext(multisetOperator, multisetQueryTuple);
	while(OperatorCall(multisetContext)) {
		Atom termForm = multisetQueryTuple[
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT)];
		if(!SameAtoms(termForm, queryTermForm))
			continue;
		// Found a multiset where the term form occurs
		Atom clauseForm = multisetQueryTuple[
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)];
		size8 multiple = multisetQueryTuple[
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)]._int;
		// Ensure the multiset is a clause form
		if(!IsClauseForm(clauseForm))
			continue;

		// If the clause is recursive, we report this to the caller in foundRecursiveClause.
		bool recursiveClauseForm = isRecursiveClauseForm(clauseForm, queryTermForm);
		if(recursiveClauseForm)
			*foundRecursiveClause = true;
		// Compile only recursive clauses in the RECURSIVE_PASS, and only non-recursive ones
		// in the NON_RECURSIVE_PASS
		if(recursiveClauseForm != (pass == RECURSIVE_PASS))
			continue;

		// Iterate over all rules (clauses) with this clause form.
		DictionaryIterator dictIterator;
		DictionaryIterate(clauseForm, &dictIterator);
		TypedTuple * matchedTermActors = CreateTypedTuple(queryTermArity);
		TypedTuple * substClauseActors = CreateTypedTuple(ClauseArity(clauseForm));
		TypedTuple * resolvedParameters = CreateTypedTuple(queryTermArity);
		while(DictionaryIteratorNext(&dictIterator)) {
			TypedTuple const * clauseActors = DictionaryIteratorPeekActors(&dictIterator);
#ifdef DEBUG_COMPILER
			PrintCString("Matched rule: ");
			PrintFormActorsAsFormula(clauseForm, clauseActors);
			PrintChar('\n');
#endif

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
					ChoiceTree choiceTree;
					resetChoiceTree(&choiceTree);
					do {
						// compileConjunction() updates parameter types in the clause
						// actors, so re-derive them for each branch.
						SubstituteTuple(&matchedTermSubst, clauseActors, substClauseActors);
#ifdef DEBUG_COMPILER
						PrintCString("Unified rule: ");
						PrintFormActorsAsFormula(clauseForm, substClauseActors);
						PrintChar('\n');
#endif

						Operator * newService = compileConjunction(
							clauseForm, substClauseActors, matchedTermIndex, queryTermForm,
							queryTermArity, &choiceTree, recursiveVariant);
						if(!newService)
							continue;
						// Recover the unified parameters from the clause actors
						TypedTupleCopyAt(substClauseActors, matchedTermActorsIndex, resolvedParameters);
						// Check for previously compiled service with the same signature
						CompiledVariant * variant = findVariant(variants, nVariants, resolvedParameters);
						if(variant) {
							// Another clause yielded the same signature: union them
							variant->op = unionOperators(variant->op, newService);
						}
						else {
							// add compiled variant of this clause
							ASSERT(nVariants < maxVariants)
							variant = &(variants[nVariants++]);
							SetMemory(variant, sizeof(CompiledVariant), 0);
							variant->parameters = CreateTypedTuple(queryTermArity);
							TypedTupleCopy(resolvedParameters, variant->parameters);
							variant->op = newService;
						}
						// A variant a recursive clause compiled into needs a fixpoint
						// operator to derive it
						variant->isRecursive = variant->isRecursive || recursiveClauseForm;
					} while(nextChoiceBranch(&choiceTree));
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
 * Set the relation (signature) for a CompiledVariant for the given query term form,
 * creating one if needed. Does nothing if the variant relation already is set.
 */
static void setupVariantRelation(
	CompiledVariant * variant, Atom queryTermForm, size8 arity)
{
	if(variant->relation)
		return;
	variant->relation = FindOrCreateRelation(
		queryTermForm, arity, getVariantTypeSignature(variant));
}


/**
 * Wrap a variant that a recursive clause compiled into in a fixpoint operator, now that its
 * clauses have compiled. That operator derives the relation, and the recurse operators
 * below it read the tuples it derives. Does nothing to a variant no recursive clause
 * compiled into.
 */
static void completeRecursiveVariant(CompiledVariant * variant, size8 arity)
{
	if(!variant->isRecursive)
		return;

	byte parameterIO[arity];
	getVariantParameterIO(variant, parameterIO);
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
 * compile first, and determine the parameter types of the query for each compiled variant.
 * The recursive clauses then compile once per variant, against the relation of that variant,
 * which their recursive term reads and takes its parameter types from. A recursive clause
 * therefore must occur together with a non-recursive clause of the same signature, else it
 * will fail to compile.
 *
 * NOTE: a recursive service is not guaranteed to terminate; see the notes on termination
 * in compiler.md.
 */
static size8 compileQueryVariants(
	Atom queryTermForm, TypedTuple const * queryActors,
	CompiledVariant * variants, size8 maxVariants)
{
	bool foundRecursiveClause;
	// First pass compilation for the non-recursive matching clauses
	size8 nVariants = compileQueryClauses(
		queryTermForm, queryActors, NON_RECURSIVE_PASS,
		0,	// recursive variant
		&foundRecursiveClause,
		variants, 0, maxVariants);
	size8 queryTermArity = TermFormArity(queryTermForm);

	if(foundRecursiveClause) {
		// Second pass, once per variant the non-recursive clauses yielded. A recursive
		// clause is compiled against one variant at a time: its recursive term reads that
		// variant's relation, and the parameter types the variant settled are what the
		// term resolves its outputs to. A recursive clause with no such variant to compile
		// against does not compile at all.
		size8 nRecursiveVariants = nVariants;
		for(index8 i = 0; i < nRecursiveVariants; i++) {
			setupVariantRelation(&variants[i], queryTermForm, queryTermArity);
			nVariants = compileQueryClauses(
				queryTermForm, variants[i].parameters, RECURSIVE_PASS, &variants[i],
				&foundRecursiveClause, variants, nVariants, maxVariants);
		}
		for(index8 i = 0; i < nVariants; i++)
			completeRecursiveVariant(&variants[i], queryTermArity);
	}

	// A service is registered against a relation, so every variant needs one. The variants
	// a recursive clause compiles against have theirs already; here we cover the rest.
	for(index8 i = 0; i < nVariants; i++)
		setupVariantRelation(&variants[i], queryTermForm, queryTermArity);
	return nVariants;
}


/**
 * The parameterized queries being compiled, outermost first. A rule body term with no
 * service is compiled from the rules, and that compilation may reach a term of a form
 * already being compiled: the rules (p x <- q x) and (q x <- p x) recurse through one
 * another. Re-entering a parameterized query already on this stack yields no service, so
 * such a clause fails to compile, as it does when no rule answers it at all.
 *
 * Recursion through a rule of the query's own form is a different matter, and is handled by
 * the recursive pass of compileQueryVariants().
 */
#define MAX_COMPILATION_DEPTH	16

typedef struct s_CompilationFrame {
	Atom termForm;
	// The parameters compileParameterizedQuery() was called with, which it holds for the
	// lifetime of the frame
	TypedTuple const * parameters;
} CompilationFrame;

static CompilationFrame compilationStack[MAX_COMPILATION_DEPTH];
static size8 compilationDepth;


/**
 * Test whether the given parameterized query is one being compiled already. A term form
 * determines the arity, so two frames of one form always have comparable parameters.
 */
static bool isBeingCompiled(Atom queryTermForm, TypedTuple const * queryParameters)
{
	for(index8 i = 0; i < compilationDepth; i++) {
		if(SameAtoms(compilationStack[i].termForm, queryTermForm)
			&& sameParameterSignature(compilationStack[i].parameters, queryParameters))
			return true;
	}
	return false;
}


/**
 * Compile a parameterized query into services, registering each one and writing a copy to
 * the services array; see CompileQuery(), which is this over the actors of a query. The
 * queryParameters tuple holds AT_PARAMETER atoms numbered 1, 2, ...
 * Returns the number of services registered, which is 0 for a query already being compiled.
 */
static size8 compileParameterizedQuery(
	Atom queryTermForm, TypedTuple const * queryParameters,
	Service services[], size8 maxServices)
{
	ASSERT(IsTermForm(queryTermForm))
	ASSERT(maxServices > 0)
	if(isBeingCompiled(queryTermForm, queryParameters))
		return 0;
	ASSERT(compilationDepth < MAX_COMPILATION_DEPTH)
	compilationStack[compilationDepth++] = (CompilationFrame) {
		.termForm = queryTermForm, .parameters = queryParameters
	};

#ifdef DEBUG_COMPILER
	PrintCString("\ncompileParameterizedQuery()\nqueryParameters: ");
	PrintFormActorsAsFormula(queryTermForm, queryParameters);
	PrintChar('\n');
#endif

	size8 arity = queryParameters->nAtoms;
	CompiledVariant variants[maxServices];
	size8 nVariants = compileQueryVariants(
		queryTermForm, queryParameters, variants, maxServices);

	for(index8 i = 0; i < nVariants; i++) {
		// Parameter types and the relation were resolved by compileQueryVariants()
		byte parameterIO[arity];
		getVariantParameterIO(&variants[i], parameterIO);
		services[i] = ServiceRegistryAdd(
			variants[i].relation, parameterIO, variants[i].op, SERVICE_COMPILED);
		// the service registry now holds the references to the operator and the relation
		ReleaseOperator(variants[i].op);
		ReleaseRelation(variants[i].relation);
		FreeTypedTuple(variants[i].parameters);
	}

#ifdef DEBUG_COMPILER
	PrintCString("-> compiled operators:\n");
	for(index8 i = 0; i < nVariants; i++) {
		PrintService(&services[i]);
		PrintChar('\n');
	}
#endif

	compilationDepth--;
	return nVariants;
}


bool FindOrCompileService(
	Atom queryTermForm, TypedTuple const * queryActors, Service * service,
	index8 permutation[])
{
	if(DispatchQuery(queryTermForm, queryActors, service, permutation))
		return true;

	size8 arity = queryActors->nAtoms;
	Atom parameters[arity];
	ActorsToParameters(queryActors, parameters);
	TypedTuple * queryParameters = CreateTypedTuple(arity);
	for(index8 i = 0; i < arity; i++)
		TypedTupleSetElement(queryParameters, i, CreateTypedAtom(AT_PARAMETER, parameters[i]));

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = compileParameterizedQuery(
		queryTermForm, queryParameters, services, MAX_COMPILED_SERVICES);
	FreeTypedTuple(queryParameters);
	if(!nServices)
		return false;

	return DispatchQuery(queryTermForm, queryActors, service, permutation);
}


size8 CompileQuery(Atom queryTerm, Service services[], size8 maxServices)
{
	FormulaView term = FormulaGetView(queryTerm);
	ASSERT(IsTermForm(term.form))

	// Parameterize the atoms of the query
	size8 arity = term.actors->nAtoms;
	Atom parameters[arity];
	ActorsToParameters(term.actors, parameters);
	// The compiler works with typed tuples of typed atoms throughout,
	// so wrap the parameters array in a TypedTuple
	TypedTuple * queryParameters = CreateTypedTuple(arity);
	for(index8 i = 0; i < arity; i++)
		TypedTupleSetElement(queryParameters, i, CreateTypedAtom(AT_PARAMETER, parameters[i]));

	size8 nVariants = compileParameterizedQuery(
		term.form, queryParameters, services, maxServices);
	FreeTypedTuple(queryParameters);
	return nVariants;
}
