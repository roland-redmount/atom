/**
 * The compiler generates new services for a query by resolving it against the rules
 * in the dictionary. The new service is implemented by a graph over Operator nodes.
 * CompileQuery() is the entry point.
 *
 * See compiler.md for additional documentation.
 *
 * Build with DEBUG_COMPILER to makes the compiler trace each query it compiles.
 */

#include "compiler/choicepoints.h"
#include "compiler/compiledvariant.h"
#include "compiler/compiler.h"
#include "compiler/compilestack.h"
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "lang/ClauseForm.h"
#include "lang/ConjunctionForm.h"
#include "lang/IndexedFormula.h"
#include "lang/SubstitutionList.h"
#include "lang/TermForm.h"
#include "lang/Variable.h"
#include "lang/unification.h"
#include "memory/allocator.h"
#include "util/ResizingArray.h"


/**
 * Prototype for a forward-referenced static functions
 */
static size8 compileParameterizedQuery(
	CompileStack * compileStack, ParameterizedQuery const * query, Service services[]);


/**
 * Generalize the actors of a term termActors tuple to parameters. 
 * Unlike ActorsToParameters(), the termActors tuple may not contain variables (AT_VARIABLE).
 * Parameters in the termActors tuple are copied directly to the parameters[] array.
 * Any non-parameter atom is considered a constant and is given the next consecutive parameter number.
 * The parameters array must hold as many atoms as the term has actors.
 */
static void termActorsToParameters(TypedTuple const * termActors, Atom parameters[])
{
	// Find the next parameter number after the highest parameter number in termActors
	uint8 nextNumber = 1;
	for(index8 i = 0; i < termActors->nAtoms; i++) {
		TypedAtom actor = TypedTupleGetElement(termActors, i);
		ASSERT(actor.type != AT_VARIABLE)
		if((actor.type == AT_PARAMETER) && (actor.atom.parameter.number >= nextNumber))
			nextNumber = actor.atom.parameter.number + 1;
	}
	for(index8 i = 0; i < termActors->nAtoms; i++) {
		TypedAtom actor = TypedTupleGetElement(termActors, i);
		if(actor.type == AT_PARAMETER) {
			// copy parameters as is
			parameters[i] = actor.atom;
		}
		else {
			// actor is a constant; assign it the next consecutive parameter number
			ASSERT(nextNumber < 255)
			parameters[i] = (Atom) {
				.parameter = {
					.number = nextNumber++, .io = PARAMETER_IN, .atomType = actor.type}
			};
		}
	}
}


/**
 * Find a service for the given term (termForm, termParameters), either by dispatch,
 * or if mode = TERM_DISPATCH_OR_COMPILE, by compiling the term.
 * The exclusion list, the resolved types and hasNextMatch are as for
 * DispatchParameterizedQuery(). Returns true if a service was obtained.
 */

#define TERM_DISPATCH_ONLY			1
#define TERM_DISPATCH_OR_COMPILE	2

static bool dispatchOrCompileTerm(
	CompileStack * compileStack, ParameterizedQuery const * query, int mode,
	Service * service, index8 permutation[],
	TypeSignature const excludedSignatures[], size8 nExcluded, bool * hasNextMatch)
{
	// CLAUDE: The term may repeat a parameter that the service does not repeat;
	// createTermOperator() then constrains the repeated arguments
	DispatchResult dispatchResult = DispatchParameterizedQuery(
		query, DISPATCH_RELAX_EQUALITY, service, permutation, excludedSignatures, nExcluded, hasNextMatch);
	// stale relations are not accepted here; they require re-compilation
	if(dispatchResult == DISPATCH_FOUND)
		return true;
	if(mode == TERM_DISPATCH_ONLY)
		return false;
	
	// Else attempt to compile new services for the term
	ASSERT(mode == TERM_DISPATCH_OR_COMPILE)
	
	// Renumber parameters 1, 2, ..., termArity since compileParameterizedQuery() expects this format. 
	// TODO: this is inconsistent -- DispatchParameterizedQuery() respects repeated
	// parameter numbers, but compileParameterizedQuery() does not. 
	// CLAUDE: Repeated parameters keep sharing a number, so the compiled service repeats them
	ParameterizedQuery queryRenumbered = *query;
	RenumberParameters(queryRenumbered.parameters, query->arity);
	compileParameterizedQuery(compileStack, &queryRenumbered,	0);

	// Re-dispatch, even if no new service was registered. Compilation may have only
	// cleared the stale flag of a primitive service, which dispatch can now accept.
	dispatchResult = DispatchParameterizedQuery(
		query, DISPATCH_RELAX_EQUALITY, service, permutation, excludedSignatures, nExcluded, hasNextMatch);
	return dispatchResult == DISPATCH_FOUND;
}


/**
 * Dispatch or compile a term, adding a choice point for it.
 * See dispatchOrCompileTerm()
 */
static bool dispatchOrCompileAtNewChoicePoint(
	CompileStack * compileStack, FormulaView term, int mode, Service * service,
	index8 permutation[], ChoiceTree * choiceTree)
{
	// Add a new choice point
	ASSERT(choiceTree->depth < MAX_CHOICE_POINTS)
	ChoicePoint * choicePoint = &(choiceTree->choicePoints[choiceTree->depth++]);

	ASSERT(choicePoint->nChoices < MAX_CHOICE_POINT_MATCHES)
#ifdef DEBUG
	if(choicePoint->nChoices)
		ASSERT(SameAtoms(choicePoint->termForm, term.form))
	choicePoint->termForm = term.form;
#endif

	// dispatch term, parameterized
	ParameterizedQuery query = {
		.form = term.form,
		.arity = term.actors->nAtoms,
	};
	termActorsToParameters(term.actors, query.parameters);

	if(!dispatchOrCompileTerm(
		compileStack, &query, mode, service, permutation,
		choicePoint->choiceSignatures, choicePoint->nChoices,
		&(choicePoint->hasNextMatch)))
	{
		// The term did not dispatch, so release the choice point
		choiceTree->depth--;
		return false;
	}

	// Add the found relation's signature to the choices for the new choice point
	choicePoint->choiceSignatures[choicePoint->nChoices] = service->relation.typeSignature;
	choicePoint->nChoices++;
	return true;
}


/**
 * Merge the arguments of a compiled term that provide the same clause argument,
 * which happens when a variable occurs more than once in the term. Emits a
 * CONSTRAIN operator yielding only those tuples in which the merged arguments are
 * equal, and compacts clauseMap accordingly, so that the clause arguments a term
 * provides are distinct.
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
	return constrainOperator;
}


/**
 * CLAUDE: Arrange the arguments of a service operator matched by dispatch into the arguments
 * of a query. The service is given by its operator and equality signature. The permutation
 * array maps each service column to a query column, as dispatch reports it. The query
 * parameters must be numbered 1, 2, ... in order of first occurrence, and the operator
 * returned takes one argument per query parameter number.
 *
 * Where the query repeats a parameter that the service does not repeat, several service
 * arguments are taken from one query argument, and a CONSTRAIN operator is returned.
 * Otherwise a PERMUTE operator is returned, unless the arguments are in order already.
 */
static Operator * arrangeServiceArguments(
	Operator * serviceOperator, EqualitySignature serviceEqualitySignature,
	Atom const queryParameters[], size8 arity, index8 const permutation[])
{
	index8 serviceArgumentMap[arity];
	size8 nServiceArguments = EqualitySignatureGetArgumentMap(
		serviceEqualitySignature, arity, serviceArgumentMap);
	ASSERT(nServiceArguments == serviceOperator->nArguments)
	index8 queryArgumentMap[arity];
	size8 nQueryArguments = ParametersGetArgumentMap(queryParameters, arity, queryArgumentMap);

	index8 argumentMap[nServiceArguments];
	bool ordered = true;
	for(index8 i = 0; i < arity; i++) {
		index8 serviceArgument = serviceArgumentMap[i];
		argumentMap[serviceArgument] = queryArgumentMap[permutation[i]];
		if(argumentMap[serviceArgument] != serviceArgument)
			ordered = false;
	}
	if(nQueryArguments < nServiceArguments)
		return CreateConstrainOperator(nQueryArguments, argumentMap, serviceOperator);
	else if(ordered)
		return serviceOperator;
	else
		return CreatePermuteOperator(nQueryArguments, 0, 0, 0, argumentMap, serviceOperator);
}


/**
 * Build the operator of a term from its matching service operator, which is given by the column
 * types it reads, its parameter IO and its operator.
 * 
 * Any non-parameter in termActors is a constant, which is provided to the service's operator,
 * by a PERMUTE operator wrapped around it. The permutation needs no operator of its own:
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
static Operator * createTermOperator(
	TypeSignature typeSignature, IOSignature ioSignature, EqualitySignature equalitySignature,
	Operator * serviceOperator, TypedTuple const * termActors, index8 const permutation[],
	TypedTuple * serviceParameters, index8 clauseMap[])
{
	size8 termArity = termActors->nAtoms;
	// CLAUDE: The service operator takes one argument per distinct service parameter.
	// A column repeating an earlier column's parameter has no argument of its own.
	index8 serviceArgumentMap[termArity];
	size8 nServiceArguments = EqualitySignatureGetArgumentMap(
		equalitySignature, termArity, serviceArgumentMap);
	ASSERT(nServiceArguments == serviceOperator->nArguments)

	// Count the constants first: a permute operator indexes its constants after
	// its arguments, so we need the number of arguments before we can map them.
	size8 nConstants = 0;
	for(index8 i = 0; i < termArity; i++) {
		if(!equalitySignature.repeatOf[i]
			&& (TypedTupleGetElement(termActors, permutation[i]).type != AT_PARAMETER))
			nConstants++;
	}
	size8 nArguments = nServiceArguments - nConstants;

	// Compute the argument map for each service parameter, respecting the argument
	// permutation obtained from dispatch
	index8 argumentMap[nServiceArguments];
	Atom constants[termArity];
	byte constantTypes[termArity];
	size8 nMapped = 0;
	size8 nMappedConstants = 0;
	// loop over service parameters
	for(index8 i = 0; i < termArity; i++) {
		TypedAtom actor = TypedTupleGetElement(termActors, permutation[i]);
		TypedTupleSetElement(serviceParameters, permutation[i],
			CreateTypedAtom(
				AT_PARAMETER,
				(Atom) {
					.parameter = {
						.number = serviceArgumentMap[i] + 1,
						.atomType = typeSignature.atomTypes[i],
						.io = ioSignature.parameterIO[i]
					}
				}
			)
		);
		if(equalitySignature.repeatOf[i]) {
			// CLAUDE: The service repeats this parameter, which dispatch only allows
			// where the term repeats the actor
			ASSERT(SameTypedAtoms(actor, TypedTupleGetElement(
				termActors, permutation[equalitySignature.repeatOf[i] - 1])))
			continue;
		}
		index8 serviceArgument = serviceArgumentMap[i];
		if(actor.type == AT_PARAMETER) {
			argumentMap[serviceArgument] = nMapped;
			// parameter numbers are 1-based positions, argument maps are 0-based indices
			clauseMap[nMapped] = actor.atom.parameter.number - 1;
			nMapped++;
		}
		else {
			// a constant restricting this service argument
			ASSERT(actor.type != AT_VARIABLE)
			argumentMap[serviceArgument] = nArguments + nMappedConstants;
			constants[nMappedConstants] = actor.atom;
			constantTypes[nMappedConstants] = actor.type;
			nMappedConstants++;
		}
	}
	ASSERT(nMapped == nArguments)

	Operator * op;
	if(!nConstants) {
		// Without constants to bind, the service operator is used as it is
		op = serviceOperator;
	}
	else {
		op = CreatePermuteOperator(
			nArguments, constants, constantTypes, nConstants, argumentMap, serviceOperator);
	}
	// A variable occurring more than once in the term constrains the arguments
	// providing it to be equal
	return constrainRepeatedArguments(op, clauseMap);
}


/**
 * Compile a term into an operator by either locating an existing service,
 * or if mode = TERM_DISPATCH_OR_COMPILE by compiling a new service.
 */
static Operator * compileTerm(
	CompileStack * compileStack, FormulaView term, int mode,
	TypedTuple * serviceParameters, index8 clauseMap[], ChoiceTree * choiceTree)
{
	// attempt to locate a service for the term
	size8 termArity = term.actors->nAtoms;
	index8 permutation[termArity];
	Service termService;
	if(!dispatchOrCompileAtNewChoicePoint(
		compileStack, term, mode, &termService, permutation, choiceTree))
		return 0;
	Operator * termOperator = ServiceGetOperator(termService);

	return createTermOperator(
		termService.relation.typeSignature, termService.ioSignature, termService.equalitySignature,
		termOperator, term.actors, permutation, serviceParameters, clauseMap);
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
 * The query term index of a conjunction query, which is compiled from all of its
 * terms; see compileConjunctionQuery().
 * 
 * TODO: We need a better representation for the conjunction that compileConjunctionQuery()
 * processes, rather than work off the clause, which now may or may not have a "head" term.
 */
#define NO_QUERY_TERM			255

/**
 * CLAUDE: The terms of a compiled conjunction are either the terms of a clause, negated,
 * or the terms of a conjunction query as given.
 */
#define BODY_TERMS_NEGATED		1
#define BODY_TERMS_AS_GIVEN		2


/**
 * The state of compiling one clause into a conjunction of operators, shared by the
 * recursion over its terms. The clause actors and the termExcluded flags are updated as terms
 * compile: an actor is given its atom type once a term providing it has dispatched, and
 * a term is marked excluded once it has been compiled.
 * 
 * TODO: I think we should factor out a struct representing only the conjunction being
 * compiled, without the matched (query; head) term. We should keep a SubstitutionList that records
 * the updated parameters as they are discovered, and update the query term _after_
 * the conjunction has compiled.
 * 
 * The hitch is the recursive terms: these can only occur in a conjunction that is assocated
 * with a query term. For 
 * 
 * In refactoring this, we should think ahead to a generalized compiler that works with subsets
 * of terms, we could have a recursive *conjunction*.) We'll need some index structure that
 * identifies a subset of terms, and their argument positions.
 */
typedef struct s_ClauseCompileState {
	IndexedFormula * indexedClause;
	uint8 nTerms;
	// Total number of clause arguments, including the local variables
	size8 nArguments;

	// The term matched by the query, excluded from the conjunction.
	// To compile recursive clauses, the parameters of this term must be fully typed.
	index8 queryTermIndex;
	// The form of the query-matched term, which is the same as for a recursive term
	Atom queryTermForm;
	// Arity of the query-matched term.
	size8 queryTermArity;
	// Number of distinct query parameters. This is less than queryTermArity if
	// at least one query parameter is repeated. Query parameters are numbered
	// 1, 2, ...nQueryArguments; a parameter number > nQueryArguments is a clause-local variable. 
	size8 nQueryArguments;
	// Whether the query parameters are fully known; else recursive terms cannot compile
	bool queryParametersKnown;

	// termExcluded[i] is true for each term compiled so far, and for the matched term
	bool * termExcluded;
	// Choice points taken during the compilation of the clause
	ChoiceTree * choiceTree;
	// Set when a recursive term has compiled to a RECURSE operator
	bool hasRecurseOperator;
	// CLAUDE: BODY_TERMS_NEGATED or BODY_TERMS_AS_GIVEN
	uint32 bodyTerms;
} ClauseCompileState;


/**
 * Update the conjunction parameter types from a newly resolved term given by termIndex.
 *
 *  - in the query-matched term the parameter stays an output, and so gives the service
 *    being compiled its signature;
 *  - in the terms not yet compiled it becomes an input, as the term that just compiled
 *    is what provides it.
 *
 * Compiled terms must be marked excluded.
 * 
 * TODO: might be cleaner to write the termActors back into the conjunction tuple first,
 * then update the clause based on that?
 */
static void propagateTermParameterTypes(
	ClauseCompileState * clauseState, index8 termIndex,
	TypedTuple const * termActors, TypedTuple const * serviceParameters)
{
	ASSERT(clauseState->termExcluded[termIndex])
	for(index8 i = 0; i < termActors->nAtoms; i++) {
		TypedAtom termActor = TypedTupleGetElement(termActors, i);
		// CLAUDE: An output parameter may be typed already, when the query parameters are
		// typed for a recursive clause. The terms still to compile must take it as an
		// input all the same, or a JOIN would not constrain them by it.
		if((termActor.type != AT_PARAMETER) || (termActor.atom.parameter.io == PARAMETER_IN))
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
		if((clauseState->queryTermIndex != NO_QUERY_TERM)
			&& (parameterNumber <= clauseState->nQueryArguments)) {
			// CLAUDE: A repeated query parameter occurs at several columns of the matched term
			for(index8 k = 0; k < clauseState->queryTermArity; k++) {
				TypedAtom matchedActor = IndexedFormulaGetTermElement(
					clauseState->indexedClause, clauseState->queryTermIndex, k);
				ASSERT(matchedActor.type == AT_PARAMETER)
				if(matchedActor.atom.parameter.number == parameterNumber) {
					IndexedFormulaSetTermAtom(
						clauseState->indexedClause, clauseState->queryTermIndex, k, outputParameter);
				}
			}
		}
		// Also set the type of the parameter in the term that compiled
		// NOTE: not necessary, this term is not used for anything at this point
		IndexedFormulaSetTermAtom(
			clauseState->indexedClause, termIndex, i, outputParameter);

		// The terms still to compile take the parameter as an input
		for(index8 j = 0; j < clauseState->nTerms; j++) {
			if(clauseState->termExcluded[j])
				continue;
			index8 termArity = IndexedFormulaTermArity(clauseState->indexedClause, j);
			for(index8 k = 0; k < termArity; k++) {
				TypedAtom actor = IndexedFormulaGetTermElement(clauseState->indexedClause, j, k);
				if(SameTypedAtoms(actor, termActor))
					IndexedFormulaSetTermAtom(clauseState->indexedClause, j, k, inputParameter);
			}
		}
	}
}


/**
 * Find the indices of the input arguments of a service with the given ioSignature
 * and equalitySignature, and write them to the inputArguments array.
 * The inputArguments array must hold RELATION_MAX_ARITY indices.
 * Returns the number of inputs found. See also FindInputArguments().
 */
static size8 findInputArguments(
	IOSignature ioSignature, EqualitySignature equalitySignature, size8 nArguments,
	index8 inputArguments[])
{
	index8 argumentMap[nArguments];
	EqualitySignatureGetArgumentMap(equalitySignature, nArguments, argumentMap);
	size8 nInputs = 0;
	for(index8 i = 0; i < nArguments; i++) {
		if(!equalitySignature.repeatOf[i] && (ioSignature.parameterIO[i] == PARAMETER_IN))
			inputArguments[nInputs++] = argumentMap[i];
	}
	return nInputs;
}


/**
 * Test whether a term of the query's term form repeats an actor everywhere the
 * query-matched term repeats a parameter. Only such a term is recursive, reading the
 * relation being derived: the derived relation holds only the tuples in which the
 * repeated query parameters are equal. For example, with the query (before @1 after @1)
 * the term (before @2 after @1) asks for tuples the derived relation does not hold.
 * Such a term is compiled like any other term.
 */
static bool termRepeatsQueryParameters(
	ClauseCompileState const * clauseState, TypedTuple const * termActors)
{
	for(index8 i = 0; i < clauseState->queryTermArity; i++) {
		Atom queryParameter = IndexedFormulaGetTermAtom(
			clauseState->indexedClause, clauseState->queryTermIndex, i);
		for(index8 j = 0; j < i; j++) {
			Atom otherQueryParameter = IndexedFormulaGetTermAtom(
				clauseState->indexedClause, clauseState->queryTermIndex, j);
			if(queryParameter.parameter.number == otherQueryParameter.parameter.number) {
				// parameter i repeats in the query term
				if(!SameTypedAtoms(TypedTupleGetElement(termActors, i), TypedTupleGetElement(termActors, j)))
					return false;
			}
		}
	}
	return true;
}


/**
 * Compile a recursive term to a RECURSE operator.
 *
 * The query actors contained in state->clauseActors must be fully determined parameters
 * when calling this function, which the recursive term's actors (termActors) must match.
 *
 * The term is taken last of the clause, so every argument another term provides is already
 * an input by then and the rest are outputs: the parameter IO of the term is settled, and
 * so is the operator. Its outputs are typed from the query signature, which is where a
 * recursive term gets its types from.
 *
 * Returns 0 where the term cannot read the variant: when a typed parameter disagrees with
 * the column it reads, and when the term leaves an argument free that the query binds. The
 * derivation is keyed on what the query binds, so a term asking for less has no call
 * binding to name it; assertCallBindingIsNamed() in operator.c is the same condition where
 * the operators meet. See testCompileRecursiveTermUnboundInput().
 */
static Operator * compileRecursiveTerm(
	ClauseCompileState * state, TypedTuple const * termActors, TypedTuple * serviceParameters, index8 clauseMap[])
{
	// Determine parameters of the recursive term
	size8 termArity = termActors->nAtoms;
	ASSERT(termArity == state->queryTermArity)
	Atom termParameters[termArity];
	termActorsToParameters(termActors, termParameters);
	// The query portion of the clause actors. Must be fully typed AT_PARAMETER atoms.
	Atom const * queryParameterArray = IndexedFormulaPeekAtoms(state->indexedClause, state->queryTermIndex);

	byte atomTypes[termArity];
	byte parameterIO[termArity];
	for(index8 i = 0; i < termArity; i++) {
		// A parameter of a known type must be the type of the column it reads
		atomTypes[i] = queryParameterArray[i].parameter.atomType;
		if(termParameters[i].parameter.atomType && (termParameters[i].parameter.atomType != atomTypes[i]))
			return 0;
		// The term has to bind at least the arguments the query binds
		parameterIO[i] = termParameters[i].parameter.io;
		if((queryParameterArray[i].parameter.io == PARAMETER_IN) && (parameterIO[i] != PARAMETER_IN))
			return 0;
	}

	IOSignature ioSignature = CreateIOSignature(parameterIO, termArity);
	// CLAUDE: The derived relation repeats the parameters the query repeats, so the
	// RECURSE operator takes one argument per distinct query parameter
	EqualitySignature equalitySignature = ParametersGetEqualitySignature(queryParameterArray, termArity);
	index8 argumentMap[termArity];
	size8 nArguments = EqualitySignatureGetArgumentMap(equalitySignature, termArity, argumentMap);
	index8 inputArguments[RELATION_MAX_ARITY];
	size8 nInputs = findInputArguments(ioSignature, equalitySignature, termArity, inputArguments);
	Operator * recurseOperator = CreateRecurseOperator(nArguments, inputArguments, nInputs);

	// The term reads the derived relation directly, so its arguments are the relation
	// columns in order and no permutation is involved
	index8 permutation[termArity];
	for(index8 i = 0; i < termArity; i++)
		permutation[i] = i;
	Operator * op = createTermOperator(
		CreateTypeSignature(atomTypes, termArity), ioSignature, equalitySignature, recurseOperator,
		termActors, permutation, serviceParameters, clauseMap);
	return op;
}


/**
 * Compile a JOIN operator from the conjuction obtained by negating the clause being
 * compiled, excluding the term the query matched and any term compiled already.
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
	CompileStack * compileStack, ClauseCompileState * clauseState, uint8 nTermsExcluded, index8 clauseMap[])
{
#ifdef DEBUG_COMPILER
	PrintCString("compileConjunctionRecursive()\n");
#endif
	ASSERT(clauseState->nTerms >= 2)
	Operator * op = 0;
	// Clause arguments provided by the compiled term. A term may refer to the same
	// clause argument more than once, so it may have more arguments than the clause.
	index8 termClauseMap[clauseState->indexedClause->actors->nAtoms];

	/**
	 * Find a term that can be compiled, in three passes over the term forms of the clause:
	 * 
	 * pass = 0: Only terms that dispatch to an existing service are considered.
	 * pass = 1: Terms that do not dispatch to an existing service are compiled,
	 *           but recursive terms are not compiled.
	 * pass = 2: Only recursive terms are compiled, producing a RECURSE operator,
	 *           provided that all their types have been determined.
	 * 
	 * The 3 passes serve to prioritize the candidate terms, so that in each call to 
	 * compileConjunctionRecursive() we prefer terms in pass 0 over those in pass 1,
	 * and those in pass 1 over those in pass 2. This is a heuristic scheme aiming to
	 * obtain a more efficient operator graph: we want to determine local parameters
	 * as early as possible, to avoid generating calls to table-scanning operators which
	 * then have to be filtered. It will not always yield the optimal solution.
	 */
	for(index8 pass = 0; !op && (pass < 3); pass++) {
		// We attempt to compile terms (recursively) only in the second pass.
		int termCompileMode = (pass == 1) ? TERM_DISPATCH_OR_COMPILE : TERM_DISPATCH_ONLY;
		// Iterate over term forms in the clause form
		MultisetIterator termFormIterator;
		MultisetIterate(clauseState->indexedClause->form, AT_ID, &termFormIterator);
		size8 termIndex = 0;
		while(!op && nTermsExcluded < clauseState->nTerms && MultisetIteratorNext(&termFormIterator)) {
			ElementMultiple em = MultisetIteratorGetElement(&termFormIterator);
			if(termIndex == clauseState->queryTermIndex) {
				termIndex += em.multiple;
				continue;
			}
			Atom termForm = em.element;
			size8 termArity = TermFormArity(termForm);
			// negate the term form if necessary
			bool negate = (clauseState->bodyTerms == BODY_TERMS_NEGATED);
			Atom negatedTermForm = CreateTermForm(
				TermFormGetPredicateForm(termForm),
				negate ? !TermFormGetSign(termForm) : TermFormGetSign(termForm)
			);
			// A term of the query's own form _may_ be recursive.
			// A term of a different form is not recursive, and is skipped in pass 2
			bool isRecursiveForm = SameAtoms(negatedTermForm, clauseState->queryTermForm);
			if(!isRecursiveForm && (pass == 2)) {
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
				// TODO: this also seems unnecessary
				TypedTupleCopyAt(
					clauseState->indexedClause->actors,
					clauseState->indexedClause->termActorsIndices[termIndex],
					termActors
				);
				// A term of the same form as the query is recursive only if it repeats
				// the parameters the query repeats; see termRepeatsQueryParameters()
				bool isRecursiveTerm =
					isRecursiveForm && termRepeatsQueryParameters(clauseState, termActors);
				// Compile a recursive term only in pass 2, and only if the query parameters are known
				if((isRecursiveTerm != (pass == 2)) || (isRecursiveTerm && !clauseState->queryParametersKnown))
					continue;
#ifdef DEBUG_COMPILER
				PrintF("Pass = %d, attempting term: ", pass);
				PrintFormActorsAsFormula(negatedTermForm, termActors);
				PrintChar('\n');
#endif
				// Attempt to compile this term. A recursive term reads the relation
				// being derived and builds its own operator; every other term dispatches,
				// adding a choice point.
				if(isRecursiveTerm) {
					op = compileRecursiveTerm(clauseState, termActors, serviceParameters, termClauseMap);
				}
				else {
					op = compileTerm(
						compileStack, (FormulaView) {.form = negatedTermForm, .actors = termActors},
						termCompileMode, serviceParameters,	termClauseMap, clauseState->choiceTree
					);
				}
				if(op) {
					if(isRecursiveTerm)
						clauseState->hasRecurseOperator = true;
#ifdef DEBUG_COMPILER
					PrintCString(" => serviceParameters = ");
					TypedTuplePrint(serviceParameters);
					PrintChar('\n');
#endif
					clauseState->termExcluded[termIndex] = true;
					nTermsExcluded++;
					propagateTermParameterTypes(clauseState, termIndex, termActors, serviceParameters);

#ifdef DEBUG_COMPILER
					PrintCString("Updated clause: ");
					PrintFormActorsAsFormula(clauseState->clauseForm, clauseState->clauseActors);
					PrintChar('\n');
#endif
					break;
				}
#ifdef DEBUG_COMPILER
				else {
					PrintCString(" => no match.\n");
				}
#endif
						
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
		index8 nextClauseMap[clauseState->indexedClause->actors->nAtoms];
		Operator * nextOperator = compileConjunctionRecursive(
			compileStack, clauseState, nTermsExcluded, nextClauseMap);
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
			return joinOperator;
		}
		else {
			// Failed to compile the rest of the cojnunction
			CheckOperator(op);
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
 * Clause-local variables are variables (AT_VARIABLE atoms) in the clause actors
 * that are not present in the query-matched term. Overwrites each local variable with
 * a local parameter atom, numbered consecutively after the query parameters.
 * Constants in the clauseActors tuples (any atom type except AT_VARIABLE) are not touched.
 * Local variables may be shared between the terms of the conjunction, and are constrained
 * to be equal by the JOIN operator.
 * After compiling the JOIN, a PROJECT operator is used to drop the corresponding arguments.
 * 
 * Returns the number of unique local variables found.
 */
static size8 parameterizeLocalVariables(
	IndexedFormula * indexedClause, index8 matchedTermIndex, size8 nQueryArguments)
{
	// CLAUDE: Without a matched term, the range of actors to skip is empty
	index8 matchedTermBegin = 0;
	index8 matchedTermEnd = 0;
	if(matchedTermIndex != NO_QUERY_TERM) {
		matchedTermBegin = indexedClause->termActorsIndices[matchedTermIndex];
		matchedTermEnd = indexedClause->termActorsIndices[matchedTermIndex + 1];
	}
	size8 nLocalVariables = 0;

	for(index8 i = 0; i < indexedClause->actors->nAtoms; i++) {
		// Skip the actors of the matched term
		if((i >= matchedTermBegin) && (i < matchedTermEnd))
			continue;
		TypedAtom actor = TypedTupleGetElement(indexedClause->actors, i);
		if(actor.type != AT_VARIABLE) {
			// actor is a constant
			continue;
		}
		// The parameter type is unknown here, and is resolved by
		// compileConjunctionRecursive() once a term producing it has compiled.
		TypedAtom parameter = CreateTypedAtom(
			AT_PARAMETER,
			(Atom) {
				.parameter = {
					.number = nQueryArguments + (++nLocalVariables),
					.io = PARAMETER_OUT,
					.atomType = 0
				}
			}
		);
		// Replace this occurence and of the variable, and any followiing ones
		TypedTupleSetElement(indexedClause->actors, i, parameter);
		for(index8 j = i + 1; j < indexedClause->actors->nAtoms; j++) {
			if((j >= matchedTermBegin) && (j < matchedTermEnd))
				continue;
			TypedAtom other = TypedTupleGetElement(indexedClause->actors, j);
			if((other.type == AT_VARIABLE) && SameVariable(other.atom, actor.atom))
				TypedTupleSetElement(indexedClause->actors, j, parameter);
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
 * free the operator and return 0.
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
			CheckOperator(op);
			return 0;
		}
	}
	if(ordered)
		return op;

	Operator * permuteOperator = CreatePermuteOperator(
		clauseNArguments, 0, 0, 0, clauseMap, op);
	CheckOperator(op);
	return permuteOperator;
}


/**
 * Returns true iff the term i of an IndexedFormula contains only fully typed parameters
 */
static bool hasFullyTypedParameters(IndexedFormula * indexedFormula, index8 i)
{
	size8 nElements = IndexedFormulaTermArity(indexedFormula, i);
	for(index8 j = 0; j < nElements; j++) {
		TypedAtom parameter = IndexedFormulaGetTermElement(indexedFormula, i, j);
		if((parameter.type != AT_PARAMETER) || !parameter.atom.parameter.atomType)
			return false;
	}
	return true;
}


/**
 * Compile the conjunction formed by negating the given clause (clauseForm, clauseActors),
 * excepting the term matching the query, indicated by queryTermIndex.
 *
 * A recursive term of the clause is compiled against the signature of the query, which
 * the query-matched term carries once its parameters are typed.
 *
 * nQueryArguments is the number of unique query parameters, which equals the number of
 * arguments of the returned operator.
 * 
 * *hasRecurseOperator is set to true if a recursive term was compiled to a RECURSE operator,
 * so that the clause needs a FIXPOINT operator.
 * 
 * bodyTerms is BODY_TERMS_NEGATED for a clause. For a conjunction query it is
 * BODY_TERMS_AS_GIVEN, and matchedTermIndex is NO_MATCHED_TERM; see compileConjunctionQuery().
 */
static Operator * compileConjunction(
	CompileStack * compileStack,
	Atom clauseForm, TypedTuple * clauseActors, index8 queryTermIndex, Atom queryTermForm,
	size8 nQueryArguments, ChoiceTree * choiceTree, int bodyTerms, bool * hasRecurseOperator)
{
	uint8 clauseNTerms = ClauseFormNTerms(clauseForm);
	// index8 termActorsIndices[clauseNTerms + 1];
	// ClauseGetTermActorsIndices(clauseForm, termActorsIndices);
	// Exclude the match term from the conjunction
	IndexedFormula * indexedClause = CreateIndexedFormula(clauseForm, clauseActors);

	bool termExcluded[clauseNTerms];
	for(index8 i = 0; i < clauseNTerms; i++)
		termExcluded[i] = (i == queryTermIndex);

	// Clause-local variables are variables (AT_VARIABLE atoms) in the clause actors
	// that are not present in the query-matched term. These become additional parameters,
	// and the conjunction is compiled with this extended arguments tuple.
	size8 nLocalVariables = parameterizeLocalVariables(
		indexedClause, queryTermIndex, nQueryArguments);

	// Extract the query parameters, if present. If not, recursive clauses cannot compile.
	size8 queryTermArity = 0;
	bool queryParametersKnown = false;
	// Without a query term there are no query parameters to read a recursive term against
	if(queryTermIndex != NO_QUERY_TERM) {
		queryTermArity = IndexedFormulaTermArity(indexedClause, queryTermIndex);
		// // NOTE: if we found a non-parameter, we scrap the tuple again ...
		queryParametersKnown = hasFullyTypedParameters(indexedClause, queryTermIndex);
	}

	// Setup the initial clause state
	// TODO: this has to be modeled better, way too many fields.
	ClauseCompileState clauseState = {
		.indexedClause = indexedClause,
		.nTerms = clauseNTerms,
		.nArguments = nQueryArguments + nLocalVariables,

		.queryTermIndex = queryTermIndex,
		.queryTermForm = queryTermForm,
		.queryTermArity = queryTermArity,
		.nQueryArguments = nQueryArguments,
		.queryParametersKnown = queryParametersKnown,

		.termExcluded = termExcluded,
		.choiceTree = choiceTree,
		.bodyTerms = bodyTerms
	};

	// Compile the conjunction recursively, joining one term at a time
	index8 clauseMap[clauseActors->nAtoms];
	// CLAUDE: The matched term is excluded from the start
	uint8 nTermsExcluded = (queryTermIndex == NO_QUERY_TERM) ? 0 : 1;
	Operator * op = compileConjunctionRecursive(compileStack, &clauseState, nTermsExcluded, clauseMap);
	*hasRecurseOperator = clauseState.hasRecurseOperator;
	if(op) {	
		// The compiled terms provide the clause arguments in their own order
		op = permuteToClauseArguments(op, clauseMap, clauseState.nArguments);

		// Drop the local variable arguments again, and any duplicate tuples this creates.
		// permuteToClauseArguments() has put the arguments in clause order, so the ones
		// to keep are the leading query arguments.
		if(op && nLocalVariables) {
			index8 keptArguments[nQueryArguments];
			for(index8 i = 0; i < nQueryArguments; i++)
				keptArguments[i] = i;
			Operator * projectOperator = CreateProjectOperator(op, nQueryArguments, keptArguments);
			op = projectOperator;
		}
	}
	FreeIndexedFormula(indexedClause);
	return op;
}


/**
 * Test whether two compiled operators have the same index order (order of tuple atoms).
 * This is required to apply the UNION to the two operators.
 */
static bool sameIndexOrder(Operator const * first, Operator const * second)
{
	return CompareMemory(first->indexOrder, second->indexOrder, first->nArguments) == 0;
}


/**
 * Sort a compiled operator into the identity index order by wrapping a PROJECT() retaining
 * all columns around the operator, unless the operator already is in identity index order.
 * Takes over the caller's reference to the operator; the caller instead obtains a reference
 * to the return Operator (which may or may not be be the same as the given operator).
 */
static Operator * sortOperatorToIdentityOrder(Operator * op)
{
	if(IsIdentityPermutation(op->indexOrder, op->nArguments))
		return op;

	index8 argumentMap[op->nArguments];
	for(index8 i = 0; i < op->nArguments; i++)
		argumentMap[i] = i;
	return CreateProjectOperator(op, op->nArguments, argumentMap);
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
 * A record of a clause form that the query term form occurs in, as collected by
 * findMatchingClauseForms().
 */
typedef struct QueryClauseMatch {
	// The matched clause form
	Atom clauseForm;
	// Multiple of the query term form in the clause form
	size8 termMultiple;
	// Whether the clause is recursive for the query; see isRecursiveClauseForm()
	bool recursive;
} QueryClauseMatch;


/**
 * Collect every clause form that the given query term form occurs in, appending one
 * QueryClauseMatch for each matched clause to the given array.
 *
 * To find rules (clauses) c that contains a matching term form,
 * we query (multiset c element @term-form multiple m),
 */
static void findMatchingClauseForms(Atom queryTermForm, ResizingArray * queryClauseMatches)
{
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
		// Ensure the multiset is a clause form
		if(!IsClauseForm(clauseForm))
			continue;

		QueryClauseMatch matchedClauseForm = {
			.clauseForm = clauseForm,
			.termMultiple = multisetQueryTuple[
				CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)]._int,
			.recursive = isRecursiveClauseForm(clauseForm, queryTermForm)
		};
		ResizingArrayAppend(queryClauseMatches, &matchedClauseForm);
	}
	OperatorFreeContext(multisetContext);
}


/**
 * CLAUDE: Add a compiled operator with the given resolved parameters to the variants array.
 * An operator whose signature matches an existing variant is combined with that variant by
 * a UNION operator. Otherwise a new variant is appended, and *nVariants is incremented.
 * Returns the variant the operator was added to.
 */
static CompiledVariant * addCompiledVariant(
	CompiledVariant variants[], size8 * nVariants, Atom resolvedParameters[], size8 arity,
	Operator * conjunctionOp)
{
	// Check for previously compiled service with the same signature
	CompiledVariant * variant = FindCompiledVariant(
		variants, *nVariants, resolvedParameters, arity);
	if(variant) {
		// We already have a compiled variant with the same signature, so create a UNION.
		// If the two operators have different indexOrder, they are sorted first.
		if(!sameIndexOrder(variant->op, conjunctionOp)) {
			variant->op = sortOperatorToIdentityOrder(variant->op);
			conjunctionOp = sortOperatorToIdentityOrder(conjunctionOp);
		}
		variant->op = CreateUnionOperator(variant->op, conjunctionOp);
		// check if we replaced a seed variant
		if(variant->isSeed) {
			variant->isSeed = false;
			variant->isReplaced = true;
		}
	}
	else {
		// add compiled variant of this clause
		ASSERT(*nVariants < MAX_COMPILED_VARIANTS)
		variant = &(variants[(*nVariants)++]);
		SetMemory(variant, sizeof(CompiledVariant), 0);
		TupleCopy(resolvedParameters, variant->parameters, arity);
		variant->op = conjunctionOp;
	}
	return variant;
}


static void copyTypedTupleToArray(TypedTuple * sourceTuple, index8 startOffset, Atom destination[], size8 nAtoms)
{
	TupleCopy(TypedTuplePeekAtoms(sourceTuple) + startOffset, destination, nAtoms);
}


static void negateClause(FormulaView clause, index8 matchedTermIndex)
{

}

/**
 * Compile every rule (clause) of the matched clause form that unifies with the query.
 * 
 * The query actors must be a series of AT_PARAMETER atoms numbered 1, 2, ...
 * Some query parameter types may be unknown; the resolved parameters are written
 * to the CompiledVariant.parameters tuple.
 *
 * Recursive terms are compiled only when all parameters in query.actors have specified types,
 * so that the recursive term is well-defined.
 * 
 * If multiple clauses resolve to the same signature, they are are combined with a UNION operator.
 * Appends the new compiled variants to the variants array and returns the new number of variants
 * in the array.
 * 
 * Appends the new compiled variants to the variants array and returns the new
 * number of variants in the array.
 */
static size8 compileClauses(
	CompileStack * compileStack, ParameterizedQuery const * query, QueryClauseMatch const * queryClauseMatch,
	CompiledVariant variants[], size8 nVariants)
{
	Atom clauseForm = queryClauseMatch->clauseForm;
	// CLAUDE: The compiled operator takes one argument per distinct query parameter
	index8 queryArgumentMap[query->arity];
	size8 nQueryArguments = ParametersGetArgumentMap(query->parameters, query->arity, queryArgumentMap);

	// Iterate over all rules (clauses) with this clause form.
	DictionaryIterator dictIterator;
	DictionaryIterate(clauseForm, &dictIterator);
	TypedTuple * matchedTermActors = CreateTypedTuple(query->arity);
	TypedTuple * substClauseActors = CreateTypedTuple(ClauseArity(clauseForm));
	Atom resolvedParameters[query->arity];
	while(DictionaryIteratorNext(&dictIterator)) {
		TypedTuple const * clauseActors = DictionaryIteratorPeekActors(&dictIterator);
#ifdef DEBUG_COMPILER
		PrintCString("Matched rule: ");
		PrintFormActorsAsFormula(clauseForm, clauseActors);
		PrintChar('\n');
#endif

		// Iterate over all occurences of the query term in the matched clause
		// and find one that unifies, if any.
		index8 matchedTermActorsOffset = ClauseGetTermActorsIndex(clauseForm, query->form, 1);
		bool foundTerm = false;
		for(index8 m = 1; !foundTerm && (m <= queryClauseMatch->termMultiple); m++) {
			// extract actors for the matching term in the clause
			TypedTupleCopyAt(clauseActors, matchedTermActorsOffset, matchedTermActors);
			// unify the query with the matched term
			Substitution querySubst;
			Substitution matchedTermSubst;
			TypedTuple * queryParameters = CreateTypedTupleFromTuple(AT_PARAMETER, query->parameters, query->arity);
			foundTerm = UnifyTuples(queryParameters, matchedTermActors, &querySubst, &matchedTermSubst);
			if(foundTerm) {
				index8 matchedTermIndex = ClauseGetTermIndex(clauseForm, query->form, m);
				// Compile the conjunction once per combination of choices. A term that leaves
				// an output parameter untyped may match several services, each
				// yielding a differently typed variant of the query service.
				ChoiceTree choiceTree;
				ChoiceTreeReset(&choiceTree);
				do {
					// compileConjunction() updates parameter types in the clause
					// actors, so re-derive them for each branch.
					SubstituteTuple(&matchedTermSubst, clauseActors, substClauseActors);
#ifdef DEBUG_COMPILER
					PrintCString("Unified rule: ");
					PrintFormActorsAsFormula(clauseForm, substClauseActors);
					PrintChar('\n');
#endif
					bool hasRecurseOperator = false;
					// TODO:  here we should create the conjunction of negated terms, and give that to compileConjunction() 
					Operator * conjunctionOp = compileConjunction(
						compileStack, clauseForm, substClauseActors, matchedTermIndex, query->form,
						nQueryArguments, &choiceTree, BODY_TERMS_NEGATED, &hasRecurseOperator);
					if(!conjunctionOp)
						continue;
					// Recover the resolved parameters (with types) from the clause actors
					copyTypedTupleToArray(
						substClauseActors, matchedTermActorsOffset, resolvedParameters, query->arity);
					CompiledVariant * variant = addCompiledVariant(
						variants, &nVariants, resolvedParameters, query->arity, conjunctionOp);
					// Mark recursive variants; FIXPOINT operator is added by completeRecursiveVariant()
					// CLAUDE: A clause of a recursive clause form needs no FIXPOINT operator
					// when its term of the query form is not recursive;
					// see termRepeatsQueryParameters()
					variant->isRecursive = variant->isRecursive || hasRecurseOperator;
				} while(ChoiceTreeNextBranch(&choiceTree));
			}
			FreeSubstitution(&querySubst);
			FreeSubstitution(&matchedTermSubst);
			FreeTypedTuple(queryParameters);
			matchedTermActorsOffset += query->arity;
		}
	}
	DictionaryIteratorEnd(&dictIterator);
	FreeTypedTuple(substClauseActors);
	FreeTypedTuple(matchedTermActors);

	return nVariants;
}


/**
 * Compile a conjunction query, such as (parent x child y & parent y child z), into
 * a JOIN of its terms. A variable occurring in several terms is provided by the
 * term compiled first, and constrains the terms compiled later through the JOIN.
 * Appends the new compiled variants to the variants array and returns the new number of
 * variants in the array.
 */
static size8 compileConjunctionQuery(
	CompileStack * compileStack, ParameterizedQuery const * query,
	CompiledVariant variants[], size8 nVariants)
{
	index8 queryArgumentMap[query->arity];
	size8 nQueryArguments = ParametersGetArgumentMap(query->parameters, query->arity, queryArgumentMap);
	TypedTuple * queryActors = CreateTypedTupleFromTuple(AT_PARAMETER, query->parameters, query->arity);
	TypedTuple * conjunctionActors = CreateTypedTuple(query->arity);
	Atom resolvedParameters[query->arity];

	// Compile the conjunction once per combination of choices; see compileClauses()
	ChoiceTree choiceTree;
	ChoiceTreeReset(&choiceTree);
	do {
		// compileConjunction() updates parameter types in the actors, so copy them anew
		// for each branch
		TypedTupleCopy(queryActors, conjunctionActors);
		bool hasRecurseOperator = false;
		Operator * conjunctionOp = compileConjunction(
			compileStack, query->form, conjunctionActors, NO_QUERY_TERM, query->form,
			nQueryArguments, &choiceTree, BODY_TERMS_AS_GIVEN, &hasRecurseOperator);
		if(!conjunctionOp)
			continue;
		ASSERT(!hasRecurseOperator)
		// The type of each query parameter is resolved in the actors of the compiled terms.
		// The parameter number and IO are those of the query.
		for(index8 i = 0; i < query->arity; i++) {
			resolvedParameters[i] = query->parameters[i];
			resolvedParameters[i].parameter.atomType =
				TypedTupleGetAtom(conjunctionActors, i).parameter.atomType;
			ASSERT(resolvedParameters[i].parameter.atomType)
		}
		addCompiledVariant(variants, &nVariants, resolvedParameters, query->arity, conjunctionOp);
	} while(ChoiceTreeNextBranch(&choiceTree));

	FreeTypedTuple(conjunctionActors);
	FreeTypedTuple(queryActors);
	return nVariants;
}


/**
 * CLAUDE: Setup a compiled variant reading a service matched by the query with
 * DISPATCH_RELAX_EQUALITY. The given operator reads the service, and takes the arguments
 * of the service operator. The variant has the column types of the service, and the
 * parameter numbers and parameter IO of the query. The operator of the variant constrains
 * the arguments of the given operator to the repeated query parameters;
 * see arrangeServiceArguments().
 */
static void setupConstrainedVariant(
	CompiledVariant * variant, ServiceRecord const * serviceRecord, Operator * op,
	ParameterizedQuery const * query, index8 const permutation[])
{
	SetMemory(variant, sizeof(CompiledVariant), 0);
	Service service = serviceRecord->service;
	for(index8 i = 0; i < query->arity; i++) {
		variant->parameters[permutation[i]] = (Atom) {
			.parameter = {
				.number = query->parameters[permutation[i]].parameter.number,
				.atomType = service.relation.typeSignature.atomTypes[i],
				.io = query->parameters[permutation[i]].parameter.io
			}
		};
	}
	variant->op = arrangeServiceArguments(
		op, service.equalitySignature, query->parameters, query->arity, permutation);
}


/**
 * Initialize ("seed") the list of compiled variants with any existing primitive services.
 * Uses DispatchIterate() to find services, which includes services from stale relations. 
 * Returns the number of variants seeded.
 */
static size8 seedVariantsFromServices(ParameterizedQuery const * query, CompiledVariant variants[])
{
	SetMemory(variants, sizeof(CompiledVariant) * MAX_COMPILED_VARIANTS, 0);
	size8 nVariants = 0;

	// CLAUDE: A service not repeating every parameter the query repeats is constrained
	// to the query; the variant is then a compiled variant rather than a seed.
	EqualitySignature queryEqualitySignature =
		ParametersGetEqualitySignature(query->parameters, query->arity);
	index8 permutation[query->arity];
	DispatchIterator iterator;
	DispatchIterate(query, DISPATCH_RELAX_EQUALITY, permutation, &iterator);
	while(DispatchIteratorNext(&iterator)) {
		ASSERT(nVariants < MAX_COMPILED_VARIANTS)
		ServiceRecord const * serviceRecord = DispatchIteratorPeekServiceRecord(&iterator);
		bool isSeed = CompareMemory(
			&(serviceRecord->service.equalitySignature), &queryEqualitySignature,
			sizeof(EqualitySignature)) == 0;

#ifdef DEBUG
		// The service must be primitive, since compilation should never run
		// if a compiled variant already exists.
		if(isSeed)
			ASSERT(serviceRecord->op->type == OPERATOR_MACHINE)
#endif

	//    if(!IsIdentityPermutation(permutation, arity))
	// 		continue;

	   // TODO: I think this case must be handled rather than left alone.
	   // For example (+ <INT + >INT = <INT) matching against (+ x + 3 = 5).
	   // The fundamental problem here is that operator indexOrder and tuple ordering
	   // in general is not aware of role multiplicity: for (+ + =), the tuples
	   // (2 3 5) and (3 2 5) correspond to the same fact, and should be considered
	   // duplicates in the relation. No operator should produce such duplicates.
	   ASSERT(IsIdentityPermutation(permutation, query->arity))

		CompiledVariant * variant = &(variants[nVariants++]);
		if(isSeed)
			SetupCompiledVariantFromServiceRecord(variant, serviceRecord);
		else
			setupConstrainedVariant(variant, serviceRecord, serviceRecord->op, query, permutation);

#ifdef DEBUG_COMPILER
		PrintCString("Seeded variant from service: ");
		PrintService(serviceRecord->service);
		PrintChar('\n');
#endif
	}
	DispatchIteratorEnd(&iterator);
	return nVariants;
}


/**
 * Find all clause forms matching the given query and compile each to variants. Seed variants
 * must have been added to the variants[] array before this call.
 * 
 * Matched rules are processed in two passes. Non-recursive clauses compile first, and determine
 * the possible query type signatures. for each compiled variant. The recursive clauses then
 * compile against these type signatures. A recursive clause therefore cannot occur without
 * at least one non-recursive clause of the same signature.
 * 
 * Returns the new number of variants in the variants[] array.
 * 
 * NOTE: this does not work when the base case of recursion is a single term, such as a
 * stored tuple with a primitive service.
 */
static size8 compileQueryClauseForms(
	CompileStack * compileStack, ParameterizedQuery const * query,
	CompiledVariant variants[], size8 nVariants)
{
	// Collect all clauses matching the query term
	ResizingArray matchedClauseForms;
	CreateResizingArray(&matchedClauseForms, sizeof(QueryClauseMatch), 8);
	findMatchingClauseForms(query->form, &matchedClauseForms);
	size32 nMatchedClauseForms = matchedClauseForms.nElements;
	if(nMatchedClauseForms == 0) {
		FreeResizingArray(&matchedClauseForms);
		return nVariants;
	}

	// The non-recursive clauses compile first, settling the query parameters of each variant
	for(index32 i = 0; i < nMatchedClauseForms; i++) {
		QueryClauseMatch const * clauseMatch = ResizingArrayGetElement(&matchedClauseForms, i);
		if(!clauseMatch->recursive)
			nVariants = compileClauses(compileStack, query, clauseMatch, variants, nVariants);
	}

	// To compile a recursive clause, the query type signature must be known.
	// The possible options are the type signatures of the non-recursive variants compiled above.
	// We try all possible such type such type signatures for each recursive clause.
	size8 nNonRecursiveVariants = nVariants;
	for(index8 v = 0; v < nNonRecursiveVariants; v++) {
		ParameterizedQuery variantQuery = {
			.form = query->form,
			.arity = query->arity
		};
		TupleCopy(variants[v].parameters, variantQuery.parameters, query->arity);
		for(index32 i = 0; i < nMatchedClauseForms; i++) {
			QueryClauseMatch const * clause = ResizingArrayGetElement(&matchedClauseForms, i);
			if(clause->recursive) {
				nVariants = compileClauses(compileStack, &variantQuery, clause, variants, nVariants);
			}
		}
	}
	// If compilaton succeeds, a recursive clause yields a UNION with the non-recursive variant,
	// so no new variants are added
	ASSERT(nVariants == nNonRecursiveVariants)

	FreeResizingArray(&matchedClauseForms);
	return nVariants;
}


/**
 * Wrap a FIXPOINT operator around a compiled recursive variant.
 * The FIXPOINT operator becomes the root of the operators tree, and its descendant
 * RECURSE operator reads the tuples generated by FIXPOINT to continue the recursion.
 */
static void completeRecursiveVariant(CompiledVariant * variant, size8 arity)
{
	ASSERT(variant->isRecursive)
	
	index8 inputArguments[RELATION_MAX_ARITY];
	size8 nInputs = findInputArguments(
		CompiledVariantGetIOSignature(variant, arity), CompiledVariantGetEqualitySignature(variant, arity),
		arity, inputArguments);

	Operator * fixpointOperator = CreateFixpointOperator(
		variant->op, inputArguments, nInputs);
	variant->op = fixpointOperator;
}


/**
 * Compile a FILTER operator based on a child service matching the query using "relaxed" dispatch.
 * Inputs to the FILTER operator that map to outputs in the child service are handled by
 * filtering tuples for equality. See OPERATOR_FILTER in operator.h.
 *
 * One variant is emitted per matching relation. Returns the new number of variants.
 */
static size8 compileFilterVariants(ParameterizedQuery const * query, CompiledVariant variants[], size8 nVariants)
{
	// Perform "relaxed" dispatch to search for services whose IO pattern
	// has an output everywhere the query has an output, and as few outputs as possible.
	index8 permutation[query->arity];
	DispatchIterator iterator;
	// CLAUDE: The child service may also repeat fewer parameters than the query
	DispatchIterate(query, DISPATCH_MATCH_RELAXED | DISPATCH_RELAX_EQUALITY, permutation, &iterator);
	index8 queryArgumentMap[query->arity];
	size8 nQueryArguments = ParametersGetArgumentMap(query->parameters, query->arity, queryArgumentMap);

	while(DispatchIteratorNext(&iterator)) {
		ASSERT(nVariants < MAX_COMPILED_VARIANTS)
		ServiceRecord const * childServiceRecord = DispatchIteratorPeekServiceRecord(&iterator);
		EqualitySignature childEqualitySignature = childServiceRecord->service.equalitySignature;
		index8 childArgumentMap[query->arity];
		size8 nChildArguments = EqualitySignatureGetArgumentMap(
			childEqualitySignature, query->arity, childArgumentMap);

		// The query arguments to filter are the ones that correspond to query inputs
		// but child service outputs.
		// CLAUDE: The filtered arguments are indices into the child operator arguments
		index8 filteredArguments[query->arity];
		size8 nFiltered = 0;
		for(index8 i = 0; i < query->arity; i++) {
			if(!childEqualitySignature.repeatOf[i]
				&& (query->parameters[permutation[i]].parameter.io == PARAMETER_IN)
				&& (childServiceRecord->service.ioSignature.parameterIO[i] == PARAMETER_OUT))
				filteredArguments[nFiltered++] = childArgumentMap[i];
		}
		// If there are no argument to filter, the child service is an exact match.
		// CLAUDE: unless the query repeats parameters that the child service does not
		if((nFiltered == 0) && (nChildArguments == nQueryArguments)) {
			// NOTE: This case happens when seedVariantsFromServices() finds a primitive service
			// but no compiled rule is generated; the variant is the discarded and we 
			// land here with nothing left to compile.
			continue;
		}

		// Create a new compiled variant

		// The FILTER service type signature is the same as that of the child,
		// while its IO direction is the same as that of the query.

		// The filter operator takes the arguments of the service it reads, so a form whose
		// roles repeat needs a permute operator to place them in query argument order

		// CLAUDE: The repeated parameters of the variant are those of the query. A query
		// repeating a parameter the child does not repeat needs a CONSTRAIN operator instead
		// of the permute operator; see setupConstrainedVariant().
		Operator * childOperator = childServiceRecord->op;
		Operator * filterOperator = (nFiltered > 0) ?
			CreateFilterOperator(childOperator, filteredArguments, nFiltered) : childOperator;
		CompiledVariant * variant = &(variants[nVariants++]);
		setupConstrainedVariant(variant, childServiceRecord, filterOperator, query, permutation);
		ASSERT(variant->op)
	}
	DispatchIteratorEnd(&iterator);
	return nVariants;
}


/**
 * Attempt to compile a query into one or more services (variants).
 * Returns the number of variants written to the variants array.
 */
static size8 compileQueryVariants(
	CompileStack * compileStack, ParameterizedQuery const * query, CompiledVariant variants[])
{
	// Initialize the set of variants with known primitive services as variants
	size8 nVariants = seedVariantsFromServices(query, variants);

	if(IsConjunctionForm(query->form)) {
		// Currently, a conjunction query is compiled direcly, not involving rules
		nVariants = compileConjunctionQuery(compileStack, query, variants, nVariants);
	}
	else {
		// Every matching clause compiles here, the recursive ones into the variants the
		// non-recursive ones settled
		nVariants = compileQueryClauseForms(compileStack, query, variants, nVariants);
	}

	// Any recursive variant must be completed by wrapping with a FIXPOINT operator
	for(index8 i = 0; i < nVariants; i++) {
		if(variants[i].isRecursive)
			completeRecursiveVariant(&variants[i], query->arity);
	}

	// CLAUDE: A query the rules do not answer may still be answered by filtering a service that
	// produces what the query binds; see compileFilterVariants(). The rules are tried
	// first, so a rule answering the query wins over reading a relation and filtering.
	// NOTE: what if we don't currently have a service to be filtered, but one could
	// have been compiled from rules?
	if(nVariants == 0)
		nVariants = compileFilterVariants(query, variants, nVariants);

	return nVariants;
}


/**
 * Compile a parameterized query into services, registering each one.
 * The queryParameters tuple must hold AT_PARAMETER atoms numbered 1, 2, ...
 * If the services array is not 0, a copy of each compiled service is written to it.
 * Returns the number of services registered. If the is already being compiled,
 * this function does nothing and returns 0.
 */
static size8 compileParameterizedQuery(
	CompileStack * compileStack, ParameterizedQuery const * query, Service services[])
{
#ifdef DEBUG
	// CLAUDE: The parameters must be numbered 1, 2, ... in order of first occurrence
	ParameterizedQuery renumberedQuery = *query;
	RenumberParameters(renumberedQuery.parameters, query->arity);
	for(index8 i = 0; i < query->arity; i++)
		ASSERT(renumberedQuery.parameters[i].parameter.number == query->parameters[i].parameter.number)
#endif
	// test if the query is on the compilation stack
	if(CompileStackContainsTerm(compileStack, query))
		return 0;
	CompileStackPush(compileStack, query);


#ifdef DEBUG_COMPILER
	PrintCString("\ncompileParameterizedQuery()\nqueryParameters: ");
	PrintParameterizedQuery(query);
	PrintChar('\n');
#endif
	// Compile all variants for the query
	CompiledVariant variants[MAX_COMPILED_VARIANTS];
	size8 nVariants = compileQueryVariants(compileStack, query, variants);

	// Register compiled services
	size8 nRegisteredServices = 0;
	for(index8 i = 0; i < nVariants; i++) {
		IOSignature ioSignature = CompiledVariantGetIOSignature(&variants[i], query->arity);
		Relation relation = (Relation) {
			.form = query->form,
			.typeSignature = CompiledVariantGetTypeSignature(&variants[i], query->arity),
		};
		Service service = (Service) {
			.relation = relation,
			.ioSignature = ioSignature,
			.equalitySignature = CompiledVariantGetEqualitySignature(&variants[i], query->arity)
		};
		if(variants[i].isSeed) {
			ServiceMarkNotStale(service);
			continue;
		}
		if(variants[i].isReplaced)
			RemoveService(service);
		else {
			// If a variant re-uses operator of an existing service, wrap it in an IDENTITY operator
			// so that we can attach a service (an operator can only attach to one Service).
			if(!IsNullRelation(variants[i].op->relation)) {
				variants[i].op = CreateIdentityOperator(variants[i].op);
			}
		}
		CreateService(service, variants[i].op);
		nRegisteredServices++;

#ifdef DEBUG_COMPILER
		PrintService(service);
		PrintChar('\n');
#endif
		if(services)
			services[i] = service;
	}
	// pop the query from the compilation stack
	CompileStackPop(compileStack);
	return nRegisteredServices;
}


bool DispatchOrCompileQuery(FormulaView query, Service * service, index8 permutation[])
{
	// Attempt to dispatch to an existing service
	if(DispatchQuery(query, service, permutation) == DISPATCH_FOUND)
		return true;
	// Else attempt to compile a service
	if(CompileQuery(query, 0) > 0) {
		// Attempt to dispatch again to the newly compiled services
		return DispatchQuery(query, service, permutation) == DISPATCH_FOUND;
	}
	else
		return false;
}


size8 CompileQuery(FormulaView query, Service services[])
{
	ASSERT(IsRelationForm(query.form))

	ParameterizedQuery parameterizedQuery;
	ParameterizeQuery(query, &parameterizedQuery);

	CompileStack compileStack = {0};
	size8 nVariants = compileParameterizedQuery(&compileStack, &parameterizedQuery, services);
	return nVariants;
}
