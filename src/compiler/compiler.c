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
#include "lang/TermMultiset.h"
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
			// NOTE: repeated constants will get distinct parameter numbers.
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
/* CLAUDE: The given choice point belongs to the term; the service found is added to it as a
 * new choice, and the relations of the choices already taken are excluded. A failed attempt
 * leaves the choice point unchanged. */
static bool dispatchOrCompileAtNewChoicePoint(
	CompileStack * compileStack, FormulaView term, int mode, ChoicePoint * choicePoint)
{
	ASSERT(choicePoint->nChoices < MAX_CHOICE_POINT_MATCHES)

	// dispatch term, parameterized
	ParameterizedQuery query = {
		.form = term.form,
		.arity = term.actors->nAtoms,
	};
	termActorsToParameters(term.actors, query.parameters);

	TypeSignature excludedSignatures[MAX_CHOICE_POINT_MATCHES];
	for(index8 i = 0; i < choicePoint->nChoices; i++)
		excludedSignatures[i] = choicePoint->choices[i].relation.typeSignature;
	Service service;
	index8 permutation[RELATION_MAX_ARITY];
	bool hasNextMatch = false;
	if(!dispatchOrCompileTerm(
		compileStack, &query, mode, &service, permutation,
		excludedSignatures, choicePoint->nChoices, &hasNextMatch))
		return false;

	// Add the found service to the choices of the choice point
	choicePoint->choices[choicePoint->nChoices] = service;
	choicePoint->nChoices++;
	CopyMemory(permutation, choicePoint->permutation, query.arity * sizeof(index8));
	choicePoint->hasNextMatch = hasNextMatch;
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


static Operator * buildOperatorFromChoicePoint(
	TypedTuple const * termActors, ChoicePoint const * choicePoint,
	TypedTuple * serviceParameters, index8 clauseMap[]);


/**
 * Compile a term into an operator by either locating an existing service,
 * or if mode = TERM_DISPATCH_OR_COMPILE by compiling a new service.
 */
static Operator * compileTerm(
	CompileStack * compileStack, FormulaView term, int mode, ChoicePoint * choicePoint,
	TypedTuple * serviceParameters, index8 clauseMap[])
{
	// attempt to locate a service for the term
	if(!dispatchOrCompileAtNewChoicePoint(compileStack, term, mode, choicePoint))
		return 0;
	return buildOperatorFromChoicePoint(term.actors, choicePoint, serviceParameters, clauseMap);
}


/**
 * CLAUDE: Build the operator of a term from the service of the current choice of its choice
 * point, choices[nChoices - 1], as compileTerm() does once dispatch has found the service.
 * Returns 0 if the service is no longer registered, or is stale.
 */
static Operator * buildOperatorFromChoicePoint(
	TypedTuple const * termActors, ChoicePoint const * choicePoint,
	TypedTuple * serviceParameters, index8 clauseMap[])
{
	ASSERT(choicePoint->nChoices > 0)
	Service service = choicePoint->choices[choicePoint->nChoices - 1];
	Operator * serviceOperator = ServiceGetOperator(service);
	if(!serviceOperator || ServiceIsStale(service))
		return 0;
	return createTermOperator(
		service.relation.typeSignature, service.ioSignature, service.equalitySignature,
		serviceOperator, termActors, choicePoint->permutation, serviceParameters, clauseMap);
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
 * The state of compiling a conjunction, shared by the recursion over its terms.
 * The term actors and the termExcluded flags are updated as terms compile:
 * a parameter is given its atom type once a term providing it has dispatched, and
 * a term is marked excluded once it has been compiled.
 */
typedef struct s_ClauseCompileState {
	IndexedFormula * indexedFormula;
	bool isConjunction;
	uint8 nTerms;
	// Total number of clause arguments, including the local variables
	size8 nArguments;

	// The head term is the term matched by the query, excluded from the conjunction.
	index8 headTermIndex;
	size8 headTermArity;
	// Number of query arguments, which equals the numbero of unique parameters in the
	// head term for a clause. A parameter number > nQueryArguments is local to the clause,
	// and does not occur in the query.
	size8 nQueryArguments;
	// Whether the head parameters are fully known; else recursive terms cannot compile
	// NOTE: this could be computed instead using hasFullyTypedParameters()
	bool headParametersKnown;

	// The terms of the conjunction, excluding the head term, in the order they compiled,
	// followed the terms still to compile, in clause order. The first nCompiledTerms
	// terms of this array have been successfully compiled.
	index8 * termOrder;
	size8 nConjunctionTerms;
	size8 nCompiledTerms;

	// termForms[i] is the form of term i as compiled: negated for a clause,
	// as given for a conjunction query. The head term has no entry.
	Atom * termForms;
	// termIsRecursive[i] is true if term i is recursive; see compileConjunction()
	bool * termIsRecursive;

	// Choice points taken during the compilation of the clause
	ChoiceTree * choiceTree;
	// Set when a recursive term has compiled to a RECURSE operator
	bool hasRecurseOperator;
} ClauseCompileState;


/**
 * Update the parameter types of the conjunction given newly resolved output parameters,
 * indicated by actorIndices and corresponding serviceParameters.
 * actorIndices[i] is the index into the actors tuple for the actor matched by service parameter i.
 * In the head term (if it exists), matching output parameters are updated with type.
 * In all other terms, the updated parameter becomes an input, provided by the 
 * newly compiled term(s).
 * Compiled terms must be marked excluded.
 */
static void propagateTermParameterTypes(
	ClauseCompileState * clauseState, TypedTuple const * serviceParameters, index8 const actorIndices[])
{
	TypedTuple * actors = clauseState->indexedFormula->actors;
	for(index8 i = 0; i < serviceParameters->nAtoms; i++) {
		// Construct the updated parameter
		TypedAtom termActor = TypedTupleGetElement(actors, actorIndices[i]);
		// Skip any constants
		if(termActor.type != AT_PARAMETER) 
			continue;
		// Input parameters of matched terms must be typed already
		if(termActor.atom.parameter.io == PARAMETER_IN)
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

		// Type the parameter in the head term, unless it is a local parameter
		if(!clauseState->isConjunction && (parameterNumber <= clauseState->nQueryArguments)) {
			// The parameter may occur multiple times in the head term
			for(index8 k = 0; k < clauseState->headTermArity; k++) {
				TypedAtom headParameter = IndexedFormulaGetTermElement(
					clauseState->indexedFormula, clauseState->headTermIndex, k);
				ASSERT(headParameter.type == AT_PARAMETER)
				if(headParameter.atom.parameter.number == parameterNumber) {
					// In the head term, only outputs can be untyped
					ASSERT(headParameter.atom.parameter.io == PARAMETER_OUT)
					IndexedFormulaSetTermAtom(
						clauseState->indexedFormula, clauseState->headTermIndex, k, outputParameter);
				}
			}
		}
		// Also set the type of the parameter in the term that compiled
		// NOTE: not necessary, this term is not used for anything at this point
		TypedTupleSetAtom(actors, actorIndices[i], outputParameter);

		// The terms still to compile take the parameter as an input
		for(index8 p = clauseState->nCompiledTerms; p < clauseState->nConjunctionTerms; p++) {
			index8 j = clauseState->termOrder[p];
			index8 termArity = IndexedFormulaTermArity(clauseState->indexedFormula, j);
			for(index8 k = 0; k < termArity; k++) {
				TypedAtom actor = IndexedFormulaGetTermElement(clauseState->indexedFormula, j, k);
				if(SameTypedAtoms(actor, termActor))
					IndexedFormulaSetTermAtom(clauseState->indexedFormula, j, k, inputParameter);
			}
		}
	}
}


/**
 * Arrange clauseState->termOrder so that the the terms given by termIndices are
 * marked a compiled. The terms are inserted following the current known compiled
 * terms; remaining terms that have not yet compiled are shifted right, keeping their order.
 */
static void arrangeCompiledTerms(
	ClauseCompileState * clauseState, index8 const termIndices[], size8 nTerms)
{
	index8 * termOrder = clauseState->termOrder;
	for(index8 i = 0; i < nTerms; i++) {
		// Find the term among the terms that had previously not been compiled
		index8 p = clauseState->nCompiledTerms;
		while(termOrder[p] != termIndices[i]) {
			p++;
			ASSERT(p < clauseState->nConjunctionTerms)
		}
		// Insert the term at position nCompiledTerms, shifting
		// the following terms at nCompiledTerms + 1 ... p one step to the right
		for(; p > clauseState->nCompiledTerms; p--)
			termOrder[p] = termOrder[p - 1];
		termOrder[p] = termIndices[i];
		clauseState->nCompiledTerms++;
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
 * Test whether the given term of repeats an actor everywhere the head term repeats a parameter.
 * Only such a term is recursive, assuming its form is also the same as the heads term's form.
 * For example, with the head term (before @1 after @1), the term (before @2 after @1) is not
 * recursive, since the head term's service cannot enumerate all tuples matching the term.
 */
static bool termRepeatsHeadTermParameters(ClauseCompileState const * clauseState, index8 termIndex)
{
	// We consider all pairs of parameters (p1, p2) in the head term, and 
	// the corresponding pairs of actors (a1, a2) in the given term, and test whether
	// p1 == p2 implies a1 == a2. We assume that two parameters are identical
	// whenever their parameter numbers are equal.
	for(index8 i = 0; i < clauseState->headTermArity; i++) {
		Atom headParameter1 = IndexedFormulaGetTermAtom(
			clauseState->indexedFormula, clauseState->headTermIndex, i);
		for(index8 j = 0; j < i; j++) {
			Atom headParameter2 = IndexedFormulaGetTermAtom(
				clauseState->indexedFormula, clauseState->headTermIndex, j);
			if(headParameter1.parameter.number == headParameter2.parameter.number) {
				// Parameters are identical in the head term; check actors in the given term
				TypedAtom actor1 = IndexedFormulaGetTermElement(clauseState->indexedFormula, termIndex, i);
				TypedAtom actor2 = IndexedFormulaGetTermElement(clauseState->indexedFormula, termIndex, j);
				if(!SameTypedAtoms(actor1, actor2))
					return false;
			}
		}
	}
	return true;
}


/**
 * Compile the term in state->indexedClause given by termIndex to a RECURSE operator.
 * The term must be known to be recursive, and the head parameters in state->indexedClause
 * must be fully determined.
 *
 * Returns 0 if (1) the recursive term's parameter types disagrees with the head term,
 * or (2) the recursive term has an output where the head has an input.
 * the column it reads, and when the term leaves an argument free that the query binds.
 * 
 * CLAUDE: The derivation is keyed on what the query binds, so a term asking for less has no call
 * binding to name it; assertCallBindingIsNamed() in operator.c is the same condition where
 * the operators meet. See testCompileRecursiveTermUnboundInput().
 */
static Operator * compileRecursiveTerm(
	ClauseCompileState * state, index8 termIndex, TypedTuple * serviceParameters, index8 clauseMap[])
{
	// Determine parameters of the recursive term
	size8 termArity = IndexedFormulaTermArity(state->indexedFormula, termIndex);
	ASSERT(termArity == state->headTermArity)
	TypedTuple * termActors = IndexedFormulaGetTermTuple(state->indexedFormula, termIndex);
	Atom termParameters[termArity];
	termActorsToParameters(termActors, termParameters);

	// Extract the head term parameters from the clause actors. Must be fully typed AT_PARAMETER atoms.
	Atom const * headParametersArray = IndexedFormulaPeekTermAtoms(state->indexedFormula, state->headTermIndex);

	// The recursive term's parameter types are determined by the head term's
	byte headAtomTypes[termArity];
	for(index8 i = 0; i < termArity; i++) {
		// The term parameter type must agree with the head if it is known
		headAtomTypes[i] = headParametersArray[i].parameter.atomType;
		if(termParameters[i].parameter.atomType && (termParameters[i].parameter.atomType != headAtomTypes[i])) {
			FreeTypedTuple(termActors);
			return 0;
		}
	}
	// The parameter IO is specified by the recursive term
	byte termParameterIO[termArity];
	for(index8 i = 0; i < termArity; i++) {
		// The term must have an input parameter where the head term has an input parameter
		termParameterIO[i] = termParameters[i].parameter.io;
		if((headParametersArray[i].parameter.io == PARAMETER_IN) && (termParameterIO[i] != PARAMETER_IN)) {
			FreeTypedTuple(termActors);
			return 0;
		}
	}

	IOSignature ioSignature = CreateIOSignature(termParameterIO, termArity);
	// The RECURSE operator's equality signature (repeated parameters) is determinend by the head term
	EqualitySignature equalitySignature = ParametersGetEqualitySignature(headParametersArray, termArity);
	index8 argumentMap[termArity];
	size8 nArguments = EqualitySignatureGetArgumentMap(equalitySignature, termArity, argumentMap);
	index8 inputArguments[RELATION_MAX_ARITY];
	size8 nInputs = findInputArguments(ioSignature, equalitySignature, termArity, inputArguments);
	Operator * recurseOperator = CreateRecurseOperator(nArguments, inputArguments, nInputs);

	// The RECURSE operator will read from the compiled clause operator, without permutation
	index8 permutation[termArity];
	for(index8 i = 0; i < termArity; i++)
		permutation[i] = i;
	Operator * op = createTermOperator(
		CreateTypeSignature(headAtomTypes, termArity), ioSignature, equalitySignature, recurseOperator,
		termActors, permutation, serviceParameters, clauseMap);

	FreeTypedTuple(termActors);
	return op;
}


/**
 * Mark the terms given by termIndices as compiled, and update the parameter types of
 * the conjunction from the serviceParameters of the term's operator.
 * actorIndices[i] is the index into the clause actors of the actor matched by service parameter i.
 */
static void acceptCompiledTerms(
	ClauseCompileState * clauseState, index8 const termIndices[], size8 nTerms,
	index8 const actorIndices[], TypedTuple const * serviceParameters)
{
#ifdef DEBUG_COMPILER
	PrintCString(" => serviceParameters = ");
	TypedTuplePrint(serviceParameters);
	PrintChar('\n');
#endif
	// Record the term chosen in the current choice point
	// CLAUDE: A replayed choice point records its own terms and actors again
	ChoiceTree * choiceTree = clauseState->choiceTree;
	ChoicePoint * choicePoint = &(choiceTree->choicePoints[choiceTree->nChoicePoints]);
	ASSERT(nTerms <= RELATION_MAX_ARITY)
	ASSERT(serviceParameters->nAtoms <= RELATION_MAX_ARITY)
	for(index8 i = 0; i < nTerms; i++)
		choicePoint->termIndices[i] = termIndices[i];
	choicePoint->nTerms = nTerms;
	for(index8 i = 0; i < serviceParameters->nAtoms; i++)
		choicePoint->actorIndices[i] = actorIndices[i];
	choicePoint->nActors = serviceParameters->nAtoms;
	choiceTree->nChoicePoints++;
	// Update the conjunction parameters
	arrangeCompiledTerms(clauseState, termIndices, nTerms);
	propagateTermParameterTypes(clauseState, serviceParameters, actorIndices);

#ifdef DEBUG_COMPILER
	PrintCString("Updated clause: ");
	PrintIndexedFormula(clauseState->indexedFormula);
	PrintChar('\n');
#endif
}


/**
 * Same as acceptCompiledTerms(), for the single term given by termIndex.
 */
static void acceptCompiledTerm(
	ClauseCompileState * clauseState, index8 termIndex, TypedTuple const * serviceParameters)
{
	size8 termArity = IndexedFormulaTermArity(clauseState->indexedFormula, termIndex);
	index8 actorIndices[termArity];
	IndexedFormulaGetTermActorIndices(clauseState->indexedFormula, termIndex, actorIndices);
	acceptCompiledTerms(clauseState, &termIndex, 1, actorIndices, serviceParameters);
}


/**
 * Compile the first non-excluded term of the clause that is not recursive.
 * With mode = TERM_DISPATCH_ONLY, we pick the first term that dispatches to an
 * existing service; with mode = TERM_DISPATCH_OR_COMPILE, we pick the first term
 * that can be compiled to a new service, using compileTerm().
 * Returns the resulting operator, or 0 if no term compiled.
 */
static Operator * compileNextTerm(
	CompileStack * compileStack, ClauseCompileState * clauseState, int mode, index8 termClauseMap[])
{
	Operator * op = 0;
	ChoicePoint * choicePoint = &(clauseState->choiceTree->choicePoints[clauseState->choiceTree->nChoicePoints]);
	// Iterate over the terms still to compile
	for(index8 p = clauseState->nCompiledTerms; !op && (p < clauseState->nConjunctionTerms); p++) {
		index8 termIndex = clauseState->termOrder[p];
		if(clauseState->termIsRecursive[termIndex])
			continue;
		Atom termForm = clauseState->termForms[termIndex];
		TypedTuple * termActors = IndexedFormulaGetTermTuple(clauseState->indexedFormula, termIndex);
#ifdef DEBUG_COMPILER
		PrintF("Pass = %d, %s term: ", mode, mode == TERM_DISPATCH_ONLY ? "dispatch" : "compile");
		PrintFormActorsAsFormula(termForm, termActors);
		PrintChar('\n');
#endif
		// Attempt to compile this term, determining serviceParameters and termClauseMap
		TypedTuple * serviceParameters = CreateTypedTuple(termActors->nAtoms);
		op = compileTerm(
			compileStack, (FormulaView) {.form = termForm, .actors = termActors},
			mode, choicePoint, serviceParameters, termClauseMap
		);
		if(op)
			acceptCompiledTerm(clauseState, termIndex, serviceParameters);
#ifdef DEBUG_COMPILER
		else
			PrintCString(" => no match.\n");
#endif
		FreeTypedTuple(serviceParameters);
		FreeTypedTuple(termActors);
	}
	return op;
}


/**
 * Compile the first non-excluded recursive term to a RECURSE operator; see
 * compileRecursiveTerm(). The term is then marked excluded. Returns 0 if no term compiled.
 */
static Operator * compileNextRecursiveTerm(ClauseCompileState * clauseState, index8 termClauseMap[])
{
	if(!clauseState->headParametersKnown)
		return 0;
	Operator * op = 0;
	for(index8 p = clauseState->nCompiledTerms; !op && (p < clauseState->nConjunctionTerms); p++) {
		index8 termIndex = clauseState->termOrder[p];
		if(!clauseState->termIsRecursive[termIndex])
			continue;
#ifdef DEBUG_COMPILER
		PrintCString("Pass = 3, recursive term: ");
		TypedTuple * termActors = IndexedFormulaGetTermTuple(clauseState->indexedFormula, termIndex);
		PrintFormActorsAsFormula(clauseState->termForms[termIndex], termActors);
		FreeTypedTuple(termActors);
		PrintChar('\n');
#endif
		size8 termArity = IndexedFormulaTermArity(clauseState->indexedFormula, termIndex);
		TypedTuple * serviceParameters = CreateTypedTuple(termArity);
		op = compileRecursiveTerm(clauseState, termIndex, serviceParameters, termClauseMap);
		if(op) {
			clauseState->hasRecurseOperator = true;
			acceptCompiledTerm(clauseState, termIndex, serviceParameters);
		}
#ifdef DEBUG_COMPILER
		else
			PrintCString(" => no match.\n");
#endif
		FreeTypedTuple(serviceParameters);
	}
	return op;
}


/**
 * Compile the term of the current choice point again, taking the current choice of
 * the choice point, which is a choice point before the branch of the choice tree; see
 * ChoiceTreeNextBranch(). A recursive term is compiled again by compileRecursiveTerm().
 * Returns 0 if the term does not compile.
 */
static Operator * replayTerm(ClauseCompileState * clauseState, index8 termClauseMap[])
{
	ChoiceTree * choiceTree = clauseState->choiceTree;
	ChoicePoint const * choicePoint = &(choiceTree->choicePoints[choiceTree->nChoicePoints]);
	index8 const * termIndices = choicePoint->termIndices;
	TypedTuple * termActors = CreateTypedTuple(choicePoint->nActors);
	TypedTupleCopySubset(
		clauseState->indexedFormula->actors, choicePoint->actorIndices, choicePoint->nActors, termActors);
#ifdef DEBUG_COMPILER
	PrintCString("Replay term: ");
	for(index8 i = 0; i < choicePoint->nTerms; i++) {
		if(i > 0)
			PrintCString(" & ");
		TypedTuple * actors = IndexedFormulaGetTermTuple(clauseState->indexedFormula, termIndices[i]);
		PrintFormActorsAsFormula(clauseState->termForms[termIndices[i]], actors);
		FreeTypedTuple(actors);
	}
	PrintChar('\n');
#endif
	TypedTuple * serviceParameters = CreateTypedTuple(termActors->nAtoms);
	Operator * op;
	if(clauseState->termIsRecursive[termIndices[0]]) {
		// CLAUDE: A recursive term compiles on its own
		ASSERT(choicePoint->nTerms == 1)
		op = compileRecursiveTerm(clauseState, termIndices[0], serviceParameters, termClauseMap);
		if(op)
			clauseState->hasRecurseOperator = true;
	}
	else
		op = buildOperatorFromChoicePoint(termActors, choicePoint, serviceParameters, termClauseMap);

	if(op) {
		acceptCompiledTerms(
			clauseState, termIndices, choicePoint->nTerms, choicePoint->actorIndices, serviceParameters);
	}
#ifdef DEBUG_COMPILER
	else
		PrintCString(" => no match.\n");
#endif
	FreeTypedTuple(serviceParameters);
	FreeTypedTuple(termActors);
	return op;
}


/**
 * Compile the term of the current choice point, which is the branch of the choice
 * tree, taking a new choice; see ChoiceTreeNextBranch(). Returns 0 if the term does not compile.
 */
static Operator * compileNextChoice(
	CompileStack * compileStack, ClauseCompileState * clauseState, index8 termClauseMap[])
{
	ChoiceTree * choiceTree = clauseState->choiceTree;
	ChoicePoint * choicePoint = &(choiceTree->choicePoints[choiceTree->nChoicePoints]);
	// CLAUDE: The query of a set of several terms has a conjunction form, which is not
	// at hand here
	ASSERT(choicePoint->nTerms == 1)
	index8 termIndex = choicePoint->termIndices[0];
	// CLAUDE: A recursive term has no choices, and so has no next choice either
	ASSERT(!clauseState->termIsRecursive[termIndex])
	TypedTuple * termActors = IndexedFormulaGetTermTuple(clauseState->indexedFormula, termIndex);
#ifdef DEBUG_COMPILER
	PrintCString("Next choice for term: ");
	PrintFormActorsAsFormula(clauseState->termForms[termIndex], termActors);
	PrintChar('\n');
#endif
	TypedTuple * serviceParameters = CreateTypedTuple(termActors->nAtoms);
	// CLAUDE: The service of the next choice may be stale, and then compiling the term
	// is what clears it; see dispatchOrCompileTerm()
	Operator * op = compileTerm(
		compileStack, (FormulaView) {.form = clauseState->termForms[termIndex], .actors = termActors},
		TERM_DISPATCH_OR_COMPILE, choicePoint, serviceParameters, termClauseMap
	);
	if(op)
		acceptCompiledTerm(clauseState, termIndex, serviceParameters);
#ifdef DEBUG_COMPILER
	else
		PrintCString(" => no match.\n");
#endif
	FreeTypedTuple(serviceParameters);
	FreeTypedTuple(termActors);
	return op;
}


/**
 * Compile a JOIN operator from the conjunction obtained by negating the clause being
 * compiled, excluding the head (query-matched) term, and any term already compiled.
 * We iterate over all terms (negated) until we find a term that dispatches to a known service,
 * or that in turn compiles to service. We then return a JOIN operator between this service's
 * operator and the operator obtained by calling compileConjunctionRecursive() on the remaining terms.
 * If the clause contains only 1 term besides the head term, we emit its service directly
 * without a JOIN, terminating the recursion.
 *
 * The clauseMap array is set to the clause argument provided by each of its arguments.
 */
static Operator * compileConjunctionRecursive(
	CompileStack * compileStack, ClauseCompileState * clauseState, index8 clauseMap[])
{
#ifdef DEBUG_COMPILER
	PrintCString("compileConjunctionRecursive()\n");
#endif
	ASSERT(clauseState->nTerms >= 2)
	ASSERT(clauseState->nCompiledTerms < clauseState->nConjunctionTerms)
	// Clause arguments provided by the compiled term. A term may refer to the same
	// clause argument more than once, so it may have more arguments than the clause.
	index8 termClauseMap[clauseState->indexedFormula->actors->nAtoms];

	/**
	 * Find a term that can be compiled, in three passes over the term forms of the clause:
	 * 
	 * pass 1: Only terms that dispatch to an existing service are considered.
	 * pass 2: Terms that do not dispatch to an existing service are compiled,
	 *         but recursive terms are not compiled.
	 * pass 3: Only recursive terms are compiled, producing a RECURSE operator,
	 *         provided that all their types have been determined.
	 * 
	 * The 3 passes serve to prioritize the candidate terms, so that in each call to 
	 * compileConjunctionRecursive() we prefer terms in pass 0 over those in pass 1,
	 * and those in pass 1 over those in pass 2. This is a heuristic scheme aiming to
	 * obtain a more efficient operator graph: we want to determine local parameters
	 * as early as possible, to avoid generating calls to table-scanning operators which
	 * then have to be filtered. It will not always yield the optimal solution.
	 */
	// CLAUDE: A choice point up to the branch names its term. A choice point after the
	// branch starts afresh, and the passes below find its term.
	ChoiceTree * choiceTree = clauseState->choiceTree;
	index8 choicePointIndex = choiceTree->nChoicePoints;
	ASSERT(choicePointIndex < MAX_CHOICE_POINTS)
	bool hasBranch = (choiceTree->branchIndex != NO_BRANCH);
	Operator * op = 0;
	if(hasBranch && (choicePointIndex < choiceTree->branchIndex))
		op = replayTerm(clauseState, termClauseMap);
	else if(hasBranch && (choicePointIndex == choiceTree->branchIndex))
		op = compileNextChoice(compileStack, clauseState, termClauseMap);
	else {
		ChoicePoint * choicePoint = &(choiceTree->choicePoints[choicePointIndex]);
		choicePoint->nChoices = 0;
		choicePoint->hasNextMatch = false;
		// pass 1
		op = compileNextTerm(compileStack, clauseState, TERM_DISPATCH_ONLY, termClauseMap);
		// pass 2
		if(!op)
			op = compileNextTerm(compileStack, clauseState, TERM_DISPATCH_OR_COMPILE, termClauseMap);
		// pass 3
		if(!op)
			op = compileNextRecursiveTerm(clauseState, termClauseMap);
	}
	if(!op) {
		// No remaining term could be dispatched
		return 0;
	}

	if(clauseState->nCompiledTerms < clauseState->nConjunctionTerms) {
		// The operator just compiled will be the left child of the JOIN operator.
		// Recurse on remaining terms to obtain the right child operator.
		index8 rightClauseMap[clauseState->indexedFormula->actors->nAtoms];
		Operator * rightOperator = compileConjunctionRecursive(
			compileStack, clauseState, rightClauseMap);
		if(rightOperator) {
			// Map the child operator arguments onto the JOIN operator arguments,
			// and return the JOIN operator
			index8 leftMap[op->nArguments];
			index8 rightMap[rightOperator->nArguments];
			size8 nJoinArguments = setupJoinArgumentMaps(
				clauseState->nArguments,
				termClauseMap, op->nArguments,
				rightClauseMap, rightOperator->nArguments,
				clauseMap, leftMap, rightMap
			);
			return CreateJoinOperator(
				nJoinArguments, op, leftMap, rightOperator, rightMap);
		}
		else {
			// Failed to compile the rest of the conjunction
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
 * Local variables are variables (AT_VARIABLE atoms) in the clause actors
 * that are not present in the head term. Overwrites each local variable with
 * a local parameter, numbered consecutively after the head term parameters.
 * Constants in the clause actors tuple (any atom type except AT_VARIABLE) are not touched.
 * Local variables may be shared between the terms of the conjunction, and are constrained
 * to be equal by the JOIN operator.
 * After compiling the JOIN, a PROJECT operator is used to drop the corresponding arguments.
 * 
 * Returns the number of unique local variables found.
 */
static size8 parameterizeLocalVariables(
	IndexedFormula * indexedClause, index8 headTermIndex, size8 nHeadArguments)
{
	// Skip the head term, if present
	index8 headTermBegin = indexedClause->termActorsIndices[headTermIndex];
	index8 headTermEnd = indexedClause->termActorsIndices[headTermIndex + 1];
	size8 nLocalVariables = 0;

	for(index8 i = 0; i < indexedClause->actors->nAtoms; i++) {
		// Skip the actors of the matched term
		if((i >= headTermBegin) && (i < headTermEnd))
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
					.number = nHeadArguments + (++nLocalVariables),
					.io = PARAMETER_OUT,
					.atomType = 0
				}
			}
		);
		// Replace this occurence and of the variable, and any followiing ones
		TypedTupleSetElement(indexedClause->actors, i, parameter);
		for(index8 j = i + 1; j < indexedClause->actors->nAtoms; j++) {
			if((j >= headTermBegin) && (j < headTermEnd))
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
 * Setup the parts of a ClauseCompileState that are the same for a clause and a
 * conjunction: no term is excluded, no term is recursive, and no term form is set.
 * The arrays are allocated; see freeClauseCompileState().
 */
static void setupCompileStateCommon(
	ClauseCompileState * clauseState, Atom form, TypedTuple * actors, ChoiceTree * choiceTree)
{
	SetMemory(clauseState, sizeof(ClauseCompileState), 0);
	clauseState->indexedFormula = CreateIndexedFormula(form, actors);
	size8 nTerms = TermMultisetNTerms(form);
	clauseState->nTerms = nTerms;
	clauseState->termOrder = Allocate(nTerms * sizeof(index8));
	clauseState->termForms = Allocate(nTerms * sizeof(Atom));
	clauseState->termIsRecursive = Allocate(nTerms * sizeof(bool));
	for(index8 i = 0; i < nTerms; i++) {
		clauseState->termForms[i] = (Atom) {0};
		clauseState->termIsRecursive[i] = false;
	}
	clauseState->choiceTree = choiceTree;
}


/**
 * Fill termOrder with the terms of the fixed choice points of its choice tree, 
 * which may be empty or determined during compilation of previous variant.
 * Terms occur in the order they compiled, followed by the other terms of the
 * conjunction, in their original (canonical) order.
 * The head term, if any, is left out.
 */
static void setupTermOrder(ClauseCompileState * clauseState, bool hasHeadTerm)
{
	ChoiceTree const * choiceTree = clauseState->choiceTree;
	// The head term, if any, is always marked "ordered"
	bool isOrdered[clauseState->nTerms];
	for(index8 i = 0; i < clauseState->nTerms; i++)
		isOrdered[i] = hasHeadTerm && (i == clauseState->headTermIndex);
	size8 nOrdered = 0;
	size8 nOrderedChoicePoints =
		(choiceTree->branchIndex == NO_BRANCH) ? 0 : choiceTree->branchIndex + 1;
	for(index8 k = 0; k < nOrderedChoicePoints; k++) {
		ChoicePoint const * choicePoint = &(choiceTree->choicePoints[k]);
		for(index8 i = 0; i < choicePoint->nTerms; i++) {
			index8 termIndex = choicePoint->termIndices[i];
			ASSERT(!isOrdered[termIndex])
			clauseState->termOrder[nOrdered++] = termIndex;
			isOrdered[termIndex] = true;
		}
	}
	for(index8 i = 0; i < clauseState->nTerms; i++) {
		if(!isOrdered[i])
			clauseState->termOrder[nOrdered++] = i;
	}
	clauseState->nConjunctionTerms = nOrdered;
	clauseState->nCompiledTerms = 0;
	ASSERT(nOrdered <= MAX_CHOICE_POINTS)
}


/**
 * Setup the state for compiling a conjunction query. The actors must be the query
 * parameters, so there are no local variables. The operator takes one argument per distinct
 * query parameter, which is held in nQueryArguments.
 */
static void setupConjunctionCompileState(
	ClauseCompileState * clauseState, Atom conjunctionForm, TypedTuple * actors, ChoiceTree * choiceTree)
{
	ASSERT(IsConjunctionForm(conjunctionForm))
	setupCompileStateCommon(clauseState, conjunctionForm, actors, choiceTree);
	clauseState->isConjunction = true;

	index8 argumentMap[actors->nAtoms];
	size8 nArguments = ParametersGetArgumentMap(TypedTuplePeekAtoms(actors), actors->nAtoms, argumentMap);
	clauseState->nArguments = nArguments;
	clauseState->nQueryArguments = nArguments;

	IndexedFormulaIterator iterator;
	IndexedFormulaIterate(clauseState->indexedFormula, &iterator);
	while(IndexedFormulaIteratorNext(&iterator)) {
		clauseState->termForms[iterator.termIndex] = iterator.termForm;
		IFactAcquire(iterator.termForm);
	}
	IndexedFormulaIteratorEnd(&iterator);
	setupTermOrder(clauseState, false);
}


/**
 * Setup the state for compiling a clause, whose head term actors must be the query
 * parameters. The clause actors are updated as the conjunction compiles, and the resolved
 * query parameters are then read from the head term actors.
 * 
 * The conjunction is obtained by negating its terms, except the for the head (query-matched) term,
 * indicated by headTermIndex. By definition, the head term has the same form and parameters as the query.
 * nHeadArguments is the number of unique parameters in the head term.
 * A recursive term in the conjunction is compiled against the head term, once its parameters
 * types have been fully determined.
 * 
 * The function sets *hasRecurseOperator = true if a recursive term was compiled to a RECURSE operator.
 * The resulting operator must then be wrapped in a FIXPOINT operator; see completeRecursiveVariant().
 * Returns the compiled Operator.
 
 */
static void setupClauseCompileState(
	ClauseCompileState * clauseState, Atom clauseForm, TypedTuple * clauseActors,
	index8 headTermIndex, ChoiceTree * choiceTree)
{
	ASSERT(IsClauseForm(clauseForm))
	setupCompileStateCommon(clauseState, clauseForm, clauseActors, choiceTree);
	IndexedFormula * indexedClause = clauseState->indexedFormula;
	clauseState->headTermIndex = headTermIndex;

	// The head term parameters are the query parameters
	size8 headTermArity = IndexedFormulaTermArity(indexedClause, headTermIndex);
	index8 headArgumentMap[headTermArity];
	size8 nHeadArguments = ParametersGetArgumentMap(
		IndexedFormulaPeekTermAtoms(indexedClause, headTermIndex), headTermArity, headArgumentMap);
	clauseState->headTermArity = headTermArity;
	clauseState->nQueryArguments = nHeadArguments;

	// Local variables are any variables not present in the head term.
	// These become local parameters, and are added to the arguments tuple.
	size8 nLocalVariables = parameterizeLocalVariables(indexedClause, headTermIndex, nHeadArguments);
	clauseState->nArguments = nHeadArguments + nLocalVariables;

	// Test if all head parameters are known; this is required to compile a recursive term.
	clauseState->headParametersKnown = hasFullyTypedParameters(indexedClause, headTermIndex);

	// Negate the form of each term except the head term
	Atom headTermForm = {0};
	IndexedFormulaIterator iterator;
	IndexedFormulaIterate(indexedClause, &iterator);
	while(IndexedFormulaIteratorNext(&iterator)) {
		if(iterator.termIndex == headTermIndex)
			headTermForm = iterator.termForm;
		else
			clauseState->termForms[iterator.termIndex] = TermFormCreateOppositeForm(iterator.termForm);
	}
	IndexedFormulaIteratorEnd(&iterator);

	for(index8 i = 0; i < clauseState->nTerms; i++) {
		if(i == headTermIndex)
			continue;
		// A term is recursive if it has the head term 's form and repeats the same parameter
		clauseState->termIsRecursive[i] = SameAtoms(clauseState->termForms[i], headTermForm)
			&& termRepeatsHeadTermParameters(clauseState, i);
	}
	setupTermOrder(clauseState, true);
}


/**
 * Free the memory held by a ClauseCompileState, and release its term forms.
 * The actors tuple given to setupClauseCompileState() or setupConjunctionCompileState()
 * is not freed.
 */
static void freeClauseCompileState(ClauseCompileState * clauseState)
{
	for(index8 i = 0; i < clauseState->nTerms; i++) {
		if(clauseState->termForms[i].hash)
			IFactRelease(clauseState->termForms[i]);
	}
	Free(clauseState->termIsRecursive);
	Free(clauseState->termForms);
	Free(clauseState->termOrder);
	FreeIndexedFormula(clauseState->indexedFormula);
}


/**
 * Compile a conjunction described by clauseState to a JOIN operator.
 * The clauseState is setup by setupClauseCompileState() or setupConjunctionCompileState().
 */
static Operator * compileConjunction(CompileStack * compileStack, ClauseCompileState * clauseState)
{
	// CLAUDE: The run records its choice points from the first; see ChoiceTree
	clauseState->choiceTree->nChoicePoints = 0;
	// Compile the conjunction recursively, joining one term at a time
	index8 clauseMap[clauseState->indexedFormula->actors->nAtoms];
	Operator * op = compileConjunctionRecursive(compileStack, clauseState, clauseMap);

	if(op) {	
		// The compiled terms provide the clause arguments in their own order
		op = permuteToClauseArguments(op, clauseMap, clauseState->nArguments);

		// Drop the local variable arguments again, and any duplicate tuples this creates.
		// permuteToClauseArguments() has put the arguments in clause order, so the ones
		// to keep are the leading query arguments.
		size8 nHeadArguments = clauseState->nQueryArguments;
		if(op && (clauseState->nArguments > nHeadArguments)) {
			index8 keptArguments[nHeadArguments];
			for(index8 i = 0; i < nHeadArguments; i++)
				keptArguments[i] = i;
			Operator * projectOperator = CreateProjectOperator(op, nHeadArguments, keptArguments);
			op = projectOperator;
		}
	}
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
					ClauseCompileState clauseState;
					setupClauseCompileState(
						&clauseState, clauseForm, substClauseActors, matchedTermIndex, &choiceTree);
					Operator * conjunctionOp = compileConjunction(compileStack, &clauseState);
					bool hasRecurseOperator = clauseState.hasRecurseOperator;
					freeClauseCompileState(&clauseState);
					if(!conjunctionOp)
						continue;
					// Recover the resolved parameters (with types) from the clause actors
					copyTypedTupleToArray(
						substClauseActors, matchedTermActorsOffset, resolvedParameters, query->arity);
					CompiledVariant * variant = addCompiledVariant(
						variants, &nVariants, resolvedParameters, query->arity, conjunctionOp);
					// Mark recursive variants; FIXPOINT operator is added by completeRecursiveVariant()
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
		ClauseCompileState clauseState;
		setupConjunctionCompileState(&clauseState, query->form, conjunctionActors, &choiceTree);
		Operator * conjunctionOp = compileConjunction(compileStack, &clauseState);
		ASSERT(!clauseState.hasRecurseOperator)
		freeClauseCompileState(&clauseState);
		if(!conjunctionOp)
			continue;
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
