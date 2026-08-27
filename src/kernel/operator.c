
#include "btree/btree.h"
#include "kernel/operator.h"
#include "kernel/tuple.h"
#include "memory/allocator.h"
#include "util/ResizingArray.h"
#include "util/utilities.h"


/**
 * Create an execution context for a child of the given context, which the child
 * records as its parent. Contexts created by a caller outside the operator tree have
 * no parent; see OperatorCreateContext().
 */
static OperatorContext * createChildContext(
	OperatorContext * parent, Operator const * op, Atom arguments[]);


/**
 * Create an Operator and setup common fields. The index order is left undeclared,
 * for the caller to derive from its children or take from its provider.
 * The caller obtains a reference to the created operator.
 */
static Operator * createOperator(enum OperatorType type, size8 nArguments, size32 contextSize)
{
	// Every operator provides a relation, and a relation has at least one column:
	// its arity is that of a predicate form, which has at least one role
	ASSERT(nArguments > 0)
	Operator * op = Allocate(sizeof(Operator));
	SetMemory(op, sizeof(Operator), 0);
	op->type = type;
	op->nArguments = nArguments;
	op->referenceCount = 1;
	op->contextSize = contextSize;
	op->indexOrder = 0;
	return op;
}


/**
 * Allocate the indexOrder[] array of an operator, for a caller that is about to fill it in.
 */
static void allocateIndexOrder(Operator * op)
{
	// A null index order means the operator declares none, yielding at most one tuple,
	// so it must not be what an operator ends up with for want of arguments
	ASSERT(!op->indexOrder)
	ASSERT(op->nArguments > 0)
	op->indexOrder = Allocate(op->nArguments);
}


/**
 * Give an operator the identity index order, so that it yields its tuples ordered by
 * argument 0 first. This is the order of an operator that materializes its tuples into a
 * B-tree, the tuple itself being the key.
 */
static void setIdentityIndexOrder(Operator * op)
{
	allocateIndexOrder(op);
	for(index8 i = 0; i < op->nArguments; i++)
		op->indexOrder[i] = i;
}


/**
 * The index order to derive from when a child declares none, being an operator that
 * yields at most one tuple: the natural order is then as valid as any other.
 * Written into the given array, which must have room for the child's arguments.
 */
static index8 const * effectiveIndexOrder(Operator const * childOperator, index8 naturalOrder[])
{
	if(childOperator->indexOrder)
		return childOperator->indexOrder;
	for(index8 i = 0; i < childOperator->nArguments; i++)
		naturalOrder[i] = i;
	return naturalOrder;
}


#ifdef DEBUG
/**
 * Verify that an index order is a permutation of the argument indices.
 */
static void assertIsIndexOrder(index8 const indexOrder[], size8 nArguments)
{
	bool present[nArguments];
	SetMemory(present, nArguments * sizeof(bool), 0);
	for(index8 i = 0; i < nArguments; i++) {
		ASSERT(indexOrder[i] < nArguments)
		ASSERT(!present[indexOrder[i]])
		present[indexOrder[i]] = true;
	}
}
#endif


/**
 * Derive the index order of an operator that relabels its child arguments, by taking
 * the child arguments in the child's order and mapping each to the argument it provides.
 * A child argument taken from elsewhere (the constants of a permute operator) has no
 * argument to contribute, and one whose argument was already contributed by an earlier
 * child argument (the collapsed arguments of a constrain operator) contributes nothing
 * further, being equal to it in every yielded tuple.
 *
 * Relabeling the arguments of a relation of at most one tuple gives a relation of at
 * most one tuple, so a child declaring no order leaves this operator undeclared too.
 */
static void deriveIndexOrderFromChild(
	Operator * op, Operator const * childOperator, index8 const * argumentMap)
{
	if(!childOperator->indexOrder)
		return;
	allocateIndexOrder(op);
	bool contributed[op->nArguments];
	SetMemory(contributed, op->nArguments * sizeof(bool), 0);
	size8 nOrdered = 0;
	for(index8 i = 0; i < childOperator->nArguments; i++) {
		index8 argument = argumentMap[childOperator->indexOrder[i]];
		if((argument >= op->nArguments) || contributed[argument])
			continue;
		contributed[argument] = true;
		op->indexOrder[nOrdered++] = argument;
	}
	ASSERT(nOrdered == op->nArguments)
}


/**
 * Copy parent arguments to a child arguments tuple, as given by an argument map.
 */
static void scatterArguments(
	Atom const arguments[], Atom childArguments[], index8 const argumentMap[], size8 nChildArguments)
{
	for(index8 i = 0; i < nChildArguments; i++)
		childArguments[i] = arguments[argumentMap[i]];
}


/**
 * Copy a child arguments tuple back to the parent arguments, as given by an argument map.
 */
static void gatherArguments(
	Atom arguments[], Atom const childArguments[], index8 const argumentMap[], size8 nChildArguments)
{
	for(index8 i = 0; i < nChildArguments; i++)
		arguments[argumentMap[i]] = childArguments[i];
}


/**
 * Store the indices of the arguments a caller binds, which restrict the tuples an
 * operator yields. Shared by FILTER, FIXPOINT and RECURSE.
 */
static void setupInputArguments(
	index8 ** storedInputArguments, size8 * storedNInputs,
	index8 const inputArguments[], size8 nInputs, size8 nArguments)
{
	*storedNInputs = nInputs;
	if(!nInputs) {
		*storedInputArguments = 0;
		return;
	}
#ifdef DEBUG
	bool bound[nArguments];
	SetMemory(bound, nArguments * sizeof(bool), 0);
	for(index8 i = 0; i < nInputs; i++) {
		ASSERT(inputArguments[i] < nArguments)
		ASSERT(!bound[inputArguments[i]])
		bound[inputArguments[i]] = true;
	}
#endif
	*storedInputArguments = Allocate(nInputs);
	CopyMemory(inputArguments, *storedInputArguments, nInputs);
}


/**
 * Test whether a tuple agrees with the arguments the caller bound, at the arguments
 * the caller binds.
 */
static bool tupleMatchesInputArguments(
	Atom const tuple[], Atom const arguments[], index8 const inputArguments[], size8 nInputs)
{
	for(index8 i = 0; i < nInputs; i++) {
		index8 argument = inputArguments[i];
		if(CompareAtoms(tuple[argument], arguments[argument]))
			return false;
	}
	return true;
}


#ifdef DEBUG
/**
 * Verify that no two child arguments take the same argument of an operator, ignoring
 * child arguments taken from elsewhere (the constants of a permute operator).
 * Mapping several child arguments to one argument is what a constrain operator
 * expresses, and no other operator does so.
 */
static void assertArgumentsAreDistinct(
	index8 const argumentMap[], size8 nChildArguments, size8 nArguments)
{
	bool taken[nArguments];
	SetMemory(taken, nArguments * sizeof(bool), 0);
	for(index8 i = 0; i < nChildArguments; i++) {
		if(argumentMap[i] >= nArguments)
			continue;
		ASSERT(!taken[argumentMap[i]])
		taken[argumentMap[i]] = true;
	}
}
#endif


//------------------------------------- OPERATOR_PERMUTE -----------------------------------------

typedef struct s_PermuteContext {
	Atom * childArguments;
	OperatorContext * childContext;
} PermuteContext;


Operator * CreatePermuteOperator(
	size8 nArguments, Atom const constants[], byte const constantTypes[], size8 nConstants,
	index8 const argumentMap[], Operator * childOperator)
{
	// The argument map indexes the parent arguments and the constants in turn,
	// so together they must be addressable by an index8
	ASSERT(nArguments + nConstants <= 256)
	Operator * op = createOperator(OPERATOR_PERMUTE, nArguments, sizeof(PermuteContext));
	op->impl.permute.childOperator = childOperator;
	AcquireOperator(childOperator);

	op->impl.permute.nConstants = nConstants;
	if(nConstants) {
		op->impl.permute.constants = Allocate(nConstants * sizeof(Atom));
		CopyMemory(constants, op->impl.permute.constants, nConstants * sizeof(Atom));
		op->impl.permute.constantTypes = Allocate(nConstants);
		CopyMemory(constantTypes, op->impl.permute.constantTypes, nConstants);
		TupleAcquire(constantTypes, constants, nConstants);
	}
	else {
		op->impl.permute.constants = 0;
		op->impl.permute.constantTypes = 0;
	}

#ifdef DEBUG
	// Bounds check the argument map against the parent arguments and the constants,
	// and verify that the child operator provides every parent argument: a permute
	// operator that leaves an argument unwritten does not yield a valid relation.
	bool provided[nArguments];
	SetMemory(provided, nArguments * sizeof(bool), 0);
	for(index8 i = 0; i < childOperator->nArguments; i++) {
		ASSERT(argumentMap[i] < nArguments + nConstants)
		if(argumentMap[i] < nArguments)
			provided[argumentMap[i]] = true;
	}
	for(index8 i = 0; i < nArguments; i++)
		ASSERT(provided[i])
	assertArgumentsAreDistinct(argumentMap, childOperator->nArguments, nArguments);
#endif

	op->impl.permute.argumentMap = Allocate(childOperator->nArguments);
	CopyMemory(argumentMap, op->impl.permute.argumentMap, childOperator->nArguments);
	deriveIndexOrderFromChild(op, childOperator, argumentMap);
	return op;
}


static void permuteSetupContext(OperatorContext * context)
{
	PermuteContext * permuteContext = (PermuteContext *) &context->data;
	Operator const * op = context->op;
	size8 nArguments = op->nArguments;
	size8 nChildArguments = op->impl.permute.childOperator->nArguments;

	permuteContext->childArguments = Allocate(nChildArguments * sizeof(Atom));
	for(index8 i = 0; i < nChildArguments; i++) {
		// take the child argument from the parent arguments, permuted, or from the constants
		index8 index = op->impl.permute.argumentMap[i];
		permuteContext->childArguments[i] = (index < nArguments)
			? context->arguments[index]
			: op->impl.permute.constants[index - nArguments];
	}
	// setup child context
	permuteContext->childContext = createChildContext(
		context,
		op->impl.permute.childOperator,
		permuteContext->childArguments
	);
}


static bool permuteCall(OperatorContext * context)
{
	PermuteContext * permuteContext = (PermuteContext *) &context->data;
	bool success = OperatorCall(permuteContext->childContext);
	if(success) {
		Operator const * op = context->op;
		size8 nArguments = op->nArguments;
		size8 nChildArguments = op->impl.permute.childOperator->nArguments;
		// copy child result tuple back to parent arguments, permuted.
		// Constant child arguments have no parent argument to copy to.
		for(index8 i = 0; i < nChildArguments; i++) {
			index8 index = op->impl.permute.argumentMap[i];
			if(index < nArguments)
				context->arguments[index] = permuteContext->childArguments[i];
		}
	}
	return success;
}


static void teardownPermuteOperator(Operator * op)
{
	ASSERT(op->type == OPERATOR_PERMUTE)
	ReleaseOperator(op->impl.permute.childOperator);
	if(op->impl.permute.nConstants) {
		TupleRelease(
			op->impl.permute.constantTypes,
			op->impl.permute.constants,
			op->impl.permute.nConstants
		);
		Free(op->impl.permute.constants);
		Free(op->impl.permute.constantTypes);
	}
	Free(op->impl.permute.argumentMap);
}


static void permuteFinalizeContext(OperatorContext * context)
{
	PermuteContext * permuteContext = (PermuteContext *) &context->data;
	OperatorFreeContext(permuteContext->childContext);
	Free(permuteContext->childArguments);
}


//----------------------------------- OPERATOR_CONSTRAIN -----------------------------------------

typedef struct s_ConstrainContext {
	Atom * childArguments;
	OperatorContext * childContext;
} ConstrainContext;


Operator * CreateConstrainOperator(
	size8 nArguments, index8 const argumentMap[], Operator * childOperator)
{
	Operator * op = createOperator(OPERATOR_CONSTRAIN, nArguments, sizeof(ConstrainContext));
	op->impl.constrain.childOperator = childOperator;
	AcquireOperator(childOperator);

#ifdef DEBUG
	// Bounds check the argument map, and verify that the child operator provides
	// every argument, as an argument this operator does not write would not be
	// part of a relation
	bool provided[nArguments];
	SetMemory(provided, nArguments * sizeof(bool), 0);
	for(index8 i = 0; i < childOperator->nArguments; i++) {
		ASSERT(argumentMap[i] < nArguments)
		provided[argumentMap[i]] = true;
	}
	for(index8 i = 0; i < nArguments; i++)
		ASSERT(provided[i])
#endif

	op->impl.constrain.argumentMap = Allocate(childOperator->nArguments);
	CopyMemory(argumentMap, op->impl.constrain.argumentMap, childOperator->nArguments);
	deriveIndexOrderFromChild(op, childOperator, argumentMap);
	return op;
}


static void constrainSetupContext(OperatorContext * context)
{
	ConstrainContext * constrainContext = (ConstrainContext *) &context->data;
	Operator const * op = context->op;
	size8 nChildArguments = op->impl.constrain.childOperator->nArguments;

	// Constrained child arguments take the same parent argument, which is what
	// enforces the constraint when the child operator takes them as inputs
	constrainContext->childArguments = Allocate(nChildArguments * sizeof(Atom));
	scatterArguments(
		context->arguments, constrainContext->childArguments,
		op->impl.constrain.argumentMap, nChildArguments);

	constrainContext->childContext = createChildContext(
		context,
		op->impl.constrain.childOperator,
		constrainContext->childArguments
	);
}


/**
 * Test whether the child arguments taken from the same parent argument are equal.
 */
static bool constrainedArgumentsAgree(
	Atom const childArguments[], index8 const argumentMap[], size8 nChildArguments)
{
	for(index8 i = 0; i < nChildArguments; i++) {
		for(index8 j = 0; j < i; j++) {
			if((argumentMap[i] == argumentMap[j])
				&& CompareAtoms(childArguments[i], childArguments[j]))
				return false;
		}
	}
	return true;
}


static bool constrainCall(OperatorContext * context)
{
	ConstrainContext * constrainContext = (ConstrainContext *) &context->data;
	Operator const * op = context->op;
	size8 nChildArguments = op->impl.constrain.childOperator->nArguments;

	// Obtain tuples from the child operator until one satisfies the constraint
	while(OperatorCall(constrainContext->childContext)) {
		if(constrainedArgumentsAgree(
			constrainContext->childArguments,
			op->impl.constrain.argumentMap, nChildArguments)) {
			gatherArguments(
				context->arguments, constrainContext->childArguments,
				op->impl.constrain.argumentMap, nChildArguments);
			return true;
		}
	}
	return false;
}


static void teardownConstrainOperator(Operator * op)
{
	ASSERT(op->type == OPERATOR_CONSTRAIN)
	ReleaseOperator(op->impl.constrain.childOperator);
	Free(op->impl.constrain.argumentMap);
}


static void constrainFinalizeContext(OperatorContext * context)
{
	ConstrainContext * constrainContext = (ConstrainContext *) &context->data;
	OperatorFreeContext(constrainContext->childContext);
	Free(constrainContext->childArguments);
}


//------------------------------------- OPERATOR_FILTER -----------------------------------------

typedef struct s_FilterContext {
	Atom * childArguments;
	OperatorContext * childContext;
} FilterContext;


Operator * CreateFilterOperator(
	Operator * childOperator, index8 const inputArguments[], size8 nInputs)
{
	// A filter that tests nothing would yield the tuples of its child unchanged
	ASSERT(nInputs > 0)
	size8 nArguments = childOperator->nArguments;
	Operator * op = createOperator(OPERATOR_FILTER, nArguments, sizeof(FilterContext));
	op->impl.filter.childOperator = childOperator;
	AcquireOperator(childOperator);
	setupInputArguments(
		&(op->impl.filter.inputArguments), &(op->impl.filter.nInputs),
		inputArguments, nInputs, nArguments);

	// Dropping tuples leaves the remaining ones in the order the child yielded them, and
	// a child yielding at most one tuple still does so once filtered
	if(childOperator->indexOrder) {
		allocateIndexOrder(op);
		CopyMemory(childOperator->indexOrder, op->indexOrder, nArguments);
	}
	return op;
}


static void filterSetupContext(OperatorContext * context)
{
	FilterContext * filterContext = (FilterContext *) &context->data;
	Operator const * op = context->op;
	size8 nArguments = op->nArguments;

	// The child operator produces the filtered arguments, so it is called with the
	// caller's arguments in a tuple of its own. Writing the child tuple there rather than
	// into context->arguments is what keeps the bound values available to test against.
	filterContext->childArguments = Allocate(nArguments * sizeof(Atom));
	CopyMemory(context->arguments, filterContext->childArguments, nArguments * sizeof(Atom));

	filterContext->childContext = createChildContext(
		context,
		op->impl.filter.childOperator,
		filterContext->childArguments
	);
}


static bool filterCall(OperatorContext * context)
{
	FilterContext * filterContext = (FilterContext *) &context->data;
	Operator const * op = context->op;
	size8 nArguments = op->nArguments;

	// Obtain tuples from the child operator until one agrees with the bound arguments
	while(OperatorCall(filterContext->childContext)) {
		if(!tupleMatchesInputArguments(
			filterContext->childArguments, context->arguments,
			op->impl.filter.inputArguments, op->impl.filter.nInputs))
			continue;
		CopyMemory(filterContext->childArguments, context->arguments, nArguments * sizeof(Atom));
		return true;
	}
	return false;
}


static void teardownFilterOperator(Operator * op)
{
	ASSERT(op->type == OPERATOR_FILTER)
	ReleaseOperator(op->impl.filter.childOperator);
	Free(op->impl.filter.inputArguments);
}


static void filterFinalizeContext(OperatorContext * context)
{
	FilterContext * filterContext = (FilterContext *) &context->data;
	OperatorFreeContext(filterContext->childContext);
	Free(filterContext->childArguments);
}


//------------------------------------- OPERATOR_JOIN -----------------------------------------

typedef struct s_JoinContext {
	// Copy of the caller's arguments
	Atom * argumentsCopy;
	// Arguments tuples for the child operators
	Atom * leftArguments;
	Atom * rightArguments;
	// Left and right child contexts
	OperatorContext * leftContext;
	OperatorContext * rightContext;
} JoinContext;


static index8 * copyJoinArgumentMap(
	index8 const argumentMap[], size8 nChildArguments, size8 nArguments)
{
	index8 * copy = Allocate(nChildArguments);
	for(index8 i = 0; i < nChildArguments; i++) {
		// every child argument must map to a parent argument
		ASSERT(argumentMap[i] < nArguments)
		copy[i] = argumentMap[i];
	}
	return copy;
}


Operator * CreateJoinOperator(
	size8 nArguments,
	Operator * leftChild, index8 const leftMap[],
	Operator * rightChild, index8 const rightMap[])
{
	Operator * op = createOperator(OPERATOR_JOIN, nArguments, sizeof(JoinContext));
	op->impl.join.left = leftChild;
	AcquireOperator(leftChild);
	op->impl.join.right = rightChild;
	AcquireOperator(rightChild);
	op->impl.join.leftMap = copyJoinArgumentMap(leftMap, leftChild->nArguments, nArguments);
	op->impl.join.rightMap = copyJoinArgumentMap(rightMap, rightChild->nArguments, nArguments);

#ifdef DEBUG
	// The two child operators must together provide every argument: a join operator
	// that leaves an argument unwritten does not yield a valid relation.
	bool provided[nArguments];
	SetMemory(provided, nArguments * sizeof(bool), 0);
	for(index8 i = 0; i < leftChild->nArguments; i++)
		provided[leftMap[i]] = true;
	for(index8 i = 0; i < rightChild->nArguments; i++)
		provided[rightMap[i]] = true;
	for(index8 i = 0; i < nArguments; i++)
		ASSERT(provided[i])
	// An argument occurring in both maps is a join argument, but neither child
	// operator may provide the same argument twice
	assertArgumentsAreDistinct(leftMap, leftChild->nArguments, nArguments);
	assertArgumentsAreDistinct(rightMap, rightChild->nArguments, nArguments);
#endif

	// The left child gives the major key: it yields ascending and the join keeps every
	// one of its arguments. The join arguments are then constant within one left tuple,
	// so the right child orders only the arguments it does not share with the left.
	// Joining two relations of at most one tuple gives at most one tuple, and only then
	// is the join left undeclared.
	if(leftChild->indexOrder || rightChild->indexOrder) {
		index8 leftNaturalOrder[leftChild->nArguments];
		index8 rightNaturalOrder[rightChild->nArguments];
		index8 const * leftOrder = effectiveIndexOrder(leftChild, leftNaturalOrder);
		index8 const * rightOrder = effectiveIndexOrder(rightChild, rightNaturalOrder);
		allocateIndexOrder(op);
		bool ordered[nArguments];
		SetMemory(ordered, nArguments * sizeof(bool), 0);
		size8 nOrdered = 0;
		for(index8 i = 0; i < leftChild->nArguments; i++) {
			index8 argument = leftMap[leftOrder[i]];
			ordered[argument] = true;
			op->indexOrder[nOrdered++] = argument;
		}
		for(index8 i = 0; i < rightChild->nArguments; i++) {
			index8 argument = rightMap[rightOrder[i]];
			if(ordered[argument])
				continue;
			ordered[argument] = true;
			op->indexOrder[nOrdered++] = argument;
		}
		ASSERT(nOrdered == nArguments)
	}

	return op;
}


static void teardownJoinOperator(Operator * op)
{
	ASSERT(op->type == OPERATOR_JOIN)
	ReleaseOperator(op->impl.join.left);
	ReleaseOperator(op->impl.join.right);
	Free(op->impl.join.leftMap);
	Free(op->impl.join.rightMap);
}


/**
 * Obtain a tuple from the left child operator of a join operator
 * and setup the right child operator context for evaluation.
 */
static bool joinEvaluateLeft(OperatorContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	Operator const * op = context->op;
	ASSERT(joinContext->leftContext)
	// Restore the parent arguments tuple. This matters for the arguments of the right
	// child operator: without it, they would be scattered from the previous right tuple
	// below, rather than from the caller's input arguments.
	CopyMemory(joinContext->argumentsCopy, context->arguments, op->nArguments * sizeof(Atom));

	// Obtain next tuple from left operator, if any
	if(!OperatorCall(joinContext->leftContext)) {
		// no more tuples, free left child context
		OperatorFreeContext(joinContext->leftContext);
		joinContext->leftContext = 0;
		return false;
	}
	gatherArguments(
		context->arguments, joinContext->leftArguments,
		op->impl.join.leftMap, op->impl.join.left->nArguments);

	// Start a new evaluation of the right operator. Its arguments are taken from the
	// parent tuple, so the arguments it shares with the left operator are now bound
	// to the left tuple, which is what constrains the join.
	scatterArguments(
		context->arguments, joinContext->rightArguments,
		op->impl.join.rightMap, op->impl.join.right->nArguments);
	joinContext->rightContext = createChildContext(
		context,
		op->impl.join.right,
		joinContext->rightArguments
	);
	return true;
}


static void joinSetupContext(OperatorContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	Operator const * op = context->op;
	size8 nArguments = op->nArguments;

	joinContext->argumentsCopy = Allocate(nArguments * sizeof(Atom));
	CopyMemory(context->arguments, joinContext->argumentsCopy, nArguments * sizeof(Atom));
	joinContext->leftArguments = Allocate(op->impl.join.left->nArguments * sizeof(Atom));
	joinContext->rightArguments = Allocate(op->impl.join.right->nArguments * sizeof(Atom));

	// The left child operator takes its input arguments from the caller
	scatterArguments(
		context->arguments, joinContext->leftArguments,
		op->impl.join.leftMap, op->impl.join.left->nArguments);
	joinContext->leftContext = createChildContext(
		context,
		op->impl.join.left,
		joinContext->leftArguments
	);
	joinEvaluateLeft(context);
}


static bool joinCall(OperatorContext * context)
{
	/**
	 * Each call to a join operator gives one tuple from the Carthesian product
	 * of the left and right relations, constrained on any shared variables.
	 * For each tuple from the left relation, we enumerate all tuples from the right relation 
	 * (if any) and keep those that unify with the left tuple. So we perform a new
	 * query for the right relation for each tuple from the left relation.
	 * NOTE: this is more efficient if the left relation is smaller than the right,
	 * and the right relation has efficient lookup (not table-scanning).
	 */
	JoinContext * joinContext = (JoinContext *) &context->data;
	if(!joinContext->leftContext)
		return false;
	ASSERT(joinContext->rightContext)

	// attempt to obtain next tuple from right child serice
	while(!OperatorCall(joinContext->rightContext)) {
		// no more tuples from right operator, start over with new left tuple
		OperatorFreeContext(joinContext->rightContext);
		joinContext->rightContext = 0;
		if(!joinEvaluateLeft(context)) {
			// join iteration is complete
			return false;
		}
	}
	// yield the resulting tuple
	gatherArguments(
		context->arguments, joinContext->rightArguments,
		context->op->impl.join.rightMap, context->op->impl.join.right->nArguments);
	return true;
}


static void joinFinalizeContext(OperatorContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	if(joinContext->leftContext)
		OperatorFreeContext(joinContext->leftContext);
	if(joinContext->rightContext)
		OperatorFreeContext(joinContext->rightContext);
	Free(joinContext->argumentsCopy);
	Free(joinContext->leftArguments);
	Free(joinContext->rightArguments);
}


//-------------------------------------- OPERATOR_UNION ------------------------------------------


typedef struct s_UnionContext {
	OperatorContext * lookaheadContext;
	OperatorContext * nextContext;
	Atom * lookahead;
} UnionContext;


Operator * CreateUnionOperator(Operator * first, Operator * second)
{
	ASSERT(first->nArguments == second->nArguments)
	Operator * op = createOperator(OPERATOR_UNION, first->nArguments, sizeof(UnionContext));

	// Merging two ordered relations is only meaningful if they are ordered alike. A child
	// declaring no order yields at most one tuple and so is ordered alike with any other;
	// if neither declares one, the union of the two may still hold two tuples, and the
	// natural order serves to merge them.
	index8 naturalOrder[first->nArguments];
	index8 const * indexOrder = first->indexOrder
		? first->indexOrder
		: effectiveIndexOrder(second, naturalOrder);
	ASSERT(
		!first->indexOrder || !second->indexOrder
		|| (CompareMemory(first->indexOrder, second->indexOrder, first->nArguments) == 0)
	)
	allocateIndexOrder(op);
	CopyMemory(indexOrder, op->indexOrder, first->nArguments);
	op->impl._union.first = first;
	AcquireOperator(first);
	op->impl._union.second = second;
	AcquireOperator(second);
	return op;
}


static void teardownUnionOperator(Operator * op)
{
	ASSERT(op->type == OPERATOR_UNION)
	ReleaseOperator(op->impl._union.first);
	ReleaseOperator(op->impl._union.second);
}


static void swapContexts(UnionContext * unionContext)
{
	OperatorContext * tmp = unionContext->lookaheadContext;
	unionContext->lookaheadContext = unionContext->nextContext;
	unionContext->nextContext = tmp;
}

static void unionSetupContext(OperatorContext * context)
{
	UnionContext * unionContext = (UnionContext *) &context->data;
	unionContext->lookahead = Allocate(context->op->nArguments * sizeof(Atom));
	// Arbitratily assign child operators to previous and next
	// both child operators write to the arguments tuple
	unionContext->lookaheadContext = createChildContext(
		context, context->op->impl._union.first, context->arguments);
	unionContext->nextContext = createChildContext(
		context, context->op->impl._union.second, context->arguments);
	// Obtain the lookahead tuple
	if(OperatorCall(unionContext->lookaheadContext)) {
		CopyMemory(context->arguments, unionContext->lookahead, context->op->nArguments * sizeof(Atom));
	}
	else {
		// No lookahead
		OperatorFreeContext(unionContext->lookaheadContext);
		unionContext->lookaheadContext = 0;
	}	
}


/**
 * Yield the lookahead tuple once the other child operator has been exhausted, and
 * continue with the child operator the lookahead came from. That operator is left as
 * the next context, so that later calls reach it through the case below.
 */
static bool unionYieldRemainingLookahead(OperatorContext * context)
{
	UnionContext * unionContext = (UnionContext *) &context->data;
	OperatorFreeContext(unionContext->nextContext);
	unionContext->nextContext = 0;
	swapContexts(unionContext);
	CopyMemory(
		unionContext->lookahead, context->arguments,
		context->op->nArguments * sizeof(Atom));
	return true;
}


static bool unionCall(OperatorContext * context)
{
	UnionContext * unionContext = (UnionContext *) &context->data;
	size8 nArguments = context->op->nArguments;
	// We interleave tuples provided by the two operators to maintain sorted order,
	// and skip any identical tuples. At each call, we have a "lookahead" tuple from
	// one of the child operators, and we try to obtain a new tuple from the other
	// operator. If one exists, we compare it to the lookahead tuple and return the
	// preceding one; else we have exhausted one operator.
	if(!unionContext->lookaheadContext) {
		// no lookahead operator, call other operator directly
		return OperatorCall(unionContext->nextContext);
	}

	if(!OperatorCall(unionContext->nextContext))
		return unionYieldRemainingLookahead(context);

	// Compare the newly obtained arguments tuple with the lookahead tuple, in the order
	// the two child operators agree on
	index8 const * indexOrder = context->op->indexOrder;
	int8 order = TupleCompareInOrder(
		context->arguments, unionContext->lookahead, indexOrder, nArguments);
	if(order == 0) {
		// Both operators produced this tuple and the union yields it once, so we take
		// another from this operator. Only one tuple can be equal, as each operator
		// yields distinct tuples. Should this operator be exhausted, the lookahead tuple
		// is still to be yielded, and yielding it here is what keeps it from being
		// yielded a second time as the lookahead of the remaining operator.
		if(!OperatorCall(unionContext->nextContext))
			return unionYieldRemainingLookahead(context);
		order = TupleCompareInOrder(
			context->arguments, unionContext->lookahead, indexOrder, nArguments);
		ASSERT(order > 0)
	}
	if(order < 0) {
		return true;
	}
	else {
		// order > 0
		SwapMemory(context->arguments, unionContext->lookahead, nArguments * sizeof(Atom));
		swapContexts(unionContext);
		return true;
	}
}


static void unionFinalizeContext(OperatorContext * context)
{
	UnionContext * unionContext = (UnionContext *) &context->data;
	if(unionContext->lookaheadContext)
		OperatorFreeContext(unionContext->lookaheadContext);
	OperatorFreeContext(unionContext->nextContext);
	Free(unionContext->lookahead);
}


//------------------------------------- OPERATOR_PROJECT -----------------------------------------

/**
 * PROJECT keeps the child arguments named by its argument map and drops the rest.
 * Dropping arguments may leave duplicate tuples, so we enumerate the entire child relation
 * into a B-tree keyed on the kept arguments, which both removes duplicates and orders the
 * result. Materializing is what a projection generally requires: dropping an argument
 * reorders the arguments the child ordered below it, so the child's order does not carry
 * over to the projected tuples.
 */
typedef struct s_ProjectContext {
	// B-tree holding the unique, ordered tuples
	BTree * btree;
	BTreeIterator iterator;
} ProjectContext;


int8 btreeCompareTuples(void const * item1, void const * item2, size32 itemSize)
{
	return TupleCompare((Atom *) item1, (Atom *) item2, itemSize / sizeof(Atom));
}


static void projectSetupContext(OperatorContext * context)
{
	ProjectContext * projectContext = (ProjectContext *) &context->data;

	Operator const * op = context->op;
	Operator * childOperator = op->impl.project.childOperator;
	size8 nArguments = op->nArguments;
	size8 nChildArguments = childOperator->nArguments;

	// The child arguments tuple takes the caller's input arguments in the kept positions;
	// the dropped arguments are left unbound so the child enumerates them.
	Atom * childArguments = Allocate(nChildArguments * sizeof(Atom));
	SetMemory(childArguments, nChildArguments * sizeof(Atom), 0);
	for(index8 i = 0; i < nArguments; i++)
		childArguments[op->impl.project.argumentMap[i]] = context->arguments[i];

	OperatorContext * childContext = createChildContext(context, childOperator, childArguments);
	// Retrieve all tuples from the child relation, gathering the kept arguments
	projectContext->btree = BTreeCreate(
		nArguments * sizeof(Atom),
		btreeCompareTuples,
		0
	);
	Atom * projectedTuple = Allocate(nArguments * sizeof(Atom));
	while(OperatorCall(childContext)) {
		for(index8 i = 0; i < nArguments; i++)
			projectedTuple[i] = childArguments[op->impl.project.argumentMap[i]];
		BTreeInsert(projectContext->btree, projectedTuple);
	}
	Free(projectedTuple);
	OperatorFreeContext(childContext);
	Free(childArguments);
	// Setup B-tree iterator
	BTreeIterate(&projectContext->iterator, projectContext->btree);
}


static bool projectCall(OperatorContext * context)
{
	ProjectContext * projectContext = (ProjectContext *) &context->data;

	if(BTreeIteratorNext(&projectContext->iterator)) {
		CopyMemory(
			BTreeIteratorPeekItem(&projectContext->iterator),
			context->arguments,
			context->op->nArguments * sizeof(Atom)
		);
		return true;
	}
	else
		return false;
}


static void projectFinalizeContext(OperatorContext * context)
{
	ProjectContext * projectContext = (ProjectContext *) &context->data;
	BTreeIteratorEnd(&projectContext->iterator);
	BTreeFree(projectContext->btree);
}


static void teardownProjectOperator(Operator * op)
{
	ASSERT(op->type == OPERATOR_PROJECT)
	ReleaseOperator(op->impl.project.childOperator);
	Free(op->impl.project.argumentMap);
}


Operator * CreateProjectOperator(
	Operator * childOperator, size8 nArguments, index8 const argumentMap[])
{
	ASSERT(nArguments <= childOperator->nArguments)
	Operator * op = createOperator(OPERATOR_PROJECT, nArguments, sizeof(ProjectContext));
	op->impl.project.childOperator = childOperator;
	AcquireOperator(childOperator);

#ifdef DEBUG
	// Each kept argument must name a distinct child argument
	bool kept[childOperator->nArguments];
	SetMemory(kept, childOperator->nArguments * sizeof(bool), 0);
	for(index8 i = 0; i < nArguments; i++) {
		ASSERT(argumentMap[i] < childOperator->nArguments)
		ASSERT(!kept[argumentMap[i]])
		kept[argumentMap[i]] = true;
	}
#endif

	op->impl.project.argumentMap = Allocate(nArguments);
	CopyMemory(argumentMap, op->impl.project.argumentMap, nArguments);
	// The B-tree orders the projected tuples as they are laid out
	setIdentityIndexOrder(op);
	return op;
}


//------------------------------ OPERATOR_FIXPOINT, OPERATOR_RECURSE -----------------------------

/**
 * A fixpoint operator computes its relation iteratively. The calls B-tree stores all
 * caller arguments from each recursive call made so far. In each iteration, we apply
 * the child operator (representing the recursive clause) to every call. When a descendant
 * RECURSE operator is called with arguments not seen before, it adds it (??).
 * A round adding neither a tuple nor a new call (fixpoint) terminates the iteration.
 *
 * Both tables are read during an iteration -- the derived tuples by the RECURSE operators
 * and the call bindings by the round itself -- and a B-tree being read is locked against
 * modification. Each therefore has somewhere else for the round to collect into, merged
 * once the round completes.
 *
 * A round collects its tuples into a plain array, as it has no duplicates to remove: an
 * operator yields distinct tuples, and the rounds of two different call bindings are each
 * constrained by their binding, so they derive disjoint tuples. The call bindings do have
 * duplicates to remove, and collect into a B-tree: a RECURSE operator registers the
 * binding it is asked for whenever a context is created for it, which is once per tuple of
 * the join above it, so the same binding arrives many times over.
 */
// Tuples a round of the fixpoint iteration is expected to derive, before the array holding
// them has to grow
#define FIXPOINT_INITIAL_PENDING_TUPLES		64

// Uncomment this to print tracing information for FIXPOINT operator call
// #define FIXPOINT_DEBUG	1

typedef struct s_FixpointContext {
	BTree * tuples;
	ResizingArray pendingTuples;
	// Argument bindings the relation has been called with, as tuples holding the bound
	// arguments with the remaining ones zeroed
	BTree * calls;
	BTree * pendingCalls;
	BTreeIterator iterator;
	Atom * childArguments;
} FixpointContext;


typedef struct s_RecurseContext {
	BTreeIterator iterator;
} RecurseContext;


/**
 * Yield the next tuple of a derived relation that agrees with the arguments the caller
 * bound, copying it to the arguments tuple. Returns false once the relation is exhausted.
 *
 * NOTE: the bound arguments are filtered rather than sought, so a query over a derived
 * relation scans it. Seeking would need the bound arguments to be a leading part of the
 * index order, as it does for a B-tree relation.
 */
static bool yieldDerivedTuple(
	OperatorContext * context, BTreeIterator * iterator,
	index8 const inputArguments[], size8 nInputs)
{
	size8 nArguments = context->op->nArguments;
	while(BTreeIteratorNext(iterator)) {
		Atom const * tuple = BTreeIteratorPeekItem(iterator);
		if(!tupleMatchesInputArguments(tuple, context->arguments, inputArguments, nInputs))
			continue;
		CopyMemory(tuple, context->arguments, nArguments * sizeof(Atom));
		return true;
	}
	return false;
}


Operator * CreateFixpointOperator(
	Operator * childOperator, index8 const inputArguments[], size8 nInputs)
{
	size8 nArguments = childOperator->nArguments;
	Operator * op = createOperator(OPERATOR_FIXPOINT, nArguments, sizeof(FixpointContext));
	op->impl.fixpoint.childOperator = childOperator;
	AcquireOperator(childOperator);
	setupInputArguments(
		&(op->impl.fixpoint.inputArguments), &(op->impl.fixpoint.nInputs),
		inputArguments, nInputs, nArguments);

	// The derived tuples are accumulated in a B-tree ordered as they are laid out
	setIdentityIndexOrder(op);
	return op;
}


Operator * CreateRecurseOperator(
	size8 nArguments, index8 const * inputArguments, size8 nInputs)
{
	Operator * op = createOperator(OPERATOR_RECURSE, nArguments, sizeof(RecurseContext));
	setupInputArguments(
		&(op->impl.recurse.inputArguments), &(op->impl.recurse.nInputs),
		inputArguments, nInputs, nArguments);

	// This operator enumerates the tuples derived by the enclosing fixpoint operator,
	// and so yields them in the order that operator accumulated them
	setIdentityIndexOrder(op);
	return op;
}


static void teardownFixpointOperator(Operator * op)
{
	ASSERT(op->type == OPERATOR_FIXPOINT)
	ReleaseOperator(op->impl.fixpoint.childOperator);
	if(op->impl.fixpoint.inputArguments)
		Free(op->impl.fixpoint.inputArguments);
}


static void teardownRecurseOperator(Operator * op)
{
	ASSERT(op->type == OPERATOR_RECURSE)
	if(op->impl.recurse.inputArguments)
		Free(op->impl.recurse.inputArguments);
}


/**
 * Add all items in the source BTree to the destination BTree,
 * so that the destiniation becomes the union of the two tuple sets.
 */
static size32 mergeBTrees(BTree * source, BTree * destination)
{
	size32 nNewItems = 0;
	BTreeIterator iterator;
	BTreeIterate(&iterator, source);
	while(BTreeIteratorNext(&iterator)) {
		if(BTreeInsert(destination, BTreeIteratorPeekItem(&iterator)) == BTREE_INSERTED)
			nNewItems++;
	}
	BTreeIteratorEnd(&iterator);
	BTreeClear(source);
	return nNewItems;
}


/**
 * Merge the tuples a completed round collected into the relation derived so far,
 * returning the number that were not already there.
 */
static size32 mergePendingTuples(ResizingArray * pendingTuples, BTree * tuples)
{
	size32 nNewTuples = 0;
	size32 nPendingTuples = ResizingArrayNElements(pendingTuples);
	for(index32 i = 0; i < nPendingTuples; i++) {
		if(BTreeInsert(tuples, ResizingArrayGetElement(pendingTuples, i)) == BTREE_INSERTED)
			nNewTuples++;
	}
	ResizingArrayReset(pendingTuples);
	return nNewTuples;
}


/**
 * Build the call binding an arguments tuple represents, being the bound arguments with
 * the remaining ones zeroed, so that bindings compare and store as ordinary tuples.
 */
static void setupCallBinding(
	Atom binding[], Atom const arguments[],
	index8 const inputArguments[], size8 nInputs, size8 nArguments)
{
	SetMemory(binding, nArguments * sizeof(Atom), 0);
	for(index8 i = 0; i < nInputs; i++)
		binding[inputArguments[i]] = arguments[inputArguments[i]];
}


#ifdef DEBUG_FIXPOINT
/**
 * Trace what a fixpoint operator derives. Each round re-applies the rule bodies to every
 * call binding known so far, including the ones it has already derived tuples for, so the
 * same tuples are derived over and over; only the ones a round adds are new.
 *
 * The atoms print as their hash, an operator not knowing the types of its arguments.
 * Hardcoding a type here renders them readably when tracing a particular relation.
 */
static void printFixpointTuple(char const * label, Atom const tuple[], size8 nArguments)
{
	PrintCString(label);
	for(index8 i = 0; i < nArguments; i++)
		PrintF(" %llx", (unsigned long long) tuple[i].hash);
	PrintChar('\n');
}
#endif


/**
 * Apply the child operator to the given arguments and store the resulting
 * tuples in the pending tuples array.
 */
static void fixpointApplyChildOperator(OperatorContext * context, Atom const * arguments)
{
	FixpointContext * fixpointContext = (FixpointContext *) &context->data;
	Operator const * op = context->op;
	size32 tupleSize = op->nArguments * sizeof(Atom);

#ifdef DEBUG_FIXPOINT
	printFixpointTuple("  call", arguments, op->nArguments);
#endif
	CopyMemory(arguments, fixpointContext->childArguments, tupleSize);
	OperatorContext * childContext = createChildContext(
		context, op->impl.fixpoint.childOperator, fixpointContext->childArguments);
	// Iterate over all tuples generate by the child operator and store them
	// as pending tuples
	while(OperatorCall(childContext)) {
#ifdef DEBUG_FIXPOINT
		printFixpointTuple("    derived", fixpointContext->childArguments, op->nArguments);
#endif
		ResizingArrayAppend(&fixpointContext->pendingTuples, fixpointContext->childArguments);
	}
	// Freeing the child context closes the RECURSE iterators into the derived relation,
	// which must be unlocked before the round can be merged into it
	OperatorFreeContext(childContext);
}


static void fixpointSetupContext(OperatorContext * context)
{
	FixpointContext * fixpointContext = (FixpointContext *) &context->data;
	Operator const * op = context->op;
	size8 nArguments = op->nArguments;
	size32 tupleSize = nArguments * sizeof(Atom);

	fixpointContext->tuples = BTreeCreate(tupleSize, btreeCompareTuples, 0);
	CreateResizingArray(
		&fixpointContext->pendingTuples, tupleSize, FIXPOINT_INITIAL_PENDING_TUPLES);
	fixpointContext->calls = BTreeCreate(tupleSize, btreeCompareTuples, 0);
	fixpointContext->pendingCalls = BTreeCreate(tupleSize, btreeCompareTuples, 0);
	fixpointContext->childArguments = Allocate(tupleSize);

	// Add the caller's argument as the first call tuple. With no 
	// arguments this is the empty binding, and the relation is derived in full.
	Atom binding[nArguments];
	setupCallBinding(
		binding, context->arguments,
		op->impl.fixpoint.inputArguments, op->impl.fixpoint.nInputs, nArguments);
	BTreeInsert(fixpointContext->calls, binding);
	
	// Each round applies the rule bodies to every call binding known so far. The RECURSE
	// operators below add the bindings they are asked for, so the rounds reach exactly
	// the bindings the query depends on, and no more.
	size32 nNewItems;
#ifdef DEBUG_FIXPOINT
	size32 round = 0;
#endif
	do {
#ifdef DEBUG_FIXPOINT
		PrintF("FIXPOINT round %u, %u calls, %u tuples so far\n",
			++round, BTreeNItems(fixpointContext->calls), BTreeNItems(fixpointContext->tuples));
#endif
		BTreeIterator callIterator;
		BTreeIterate(&callIterator, fixpointContext->calls);
		while(BTreeIteratorNext(&callIterator))
			fixpointApplyChildOperator(context, BTreeIteratorPeekItem(&callIterator));
		BTreeIteratorEnd(&callIterator);

		nNewItems = mergePendingTuples(&fixpointContext->pendingTuples, fixpointContext->tuples);
		nNewItems += mergeBTrees(fixpointContext->pendingCalls, fixpointContext->calls);
	} while(nNewItems);

	BTreeIterate(&fixpointContext->iterator, fixpointContext->tuples);
}


static bool fixpointCall(OperatorContext * context)
{
	FixpointContext * fixpointContext = (FixpointContext *) &context->data;
	return yieldDerivedTuple(
		context, &fixpointContext->iterator,
		context->op->impl.fixpoint.inputArguments, context->op->impl.fixpoint.nInputs);
}


size32 FixpointNDerivedTuples(OperatorContext const * context)
{
	ASSERT(context->op->type == OPERATOR_FIXPOINT)
	return BTreeNItems(((FixpointContext const *) &context->data)->tuples);
}


static void fixpointFinalizeContext(OperatorContext * context)
{
	FixpointContext * fixpointContext = (FixpointContext *) &context->data;
	BTreeIteratorEnd(&fixpointContext->iterator);
	BTreeFree(fixpointContext->pendingCalls);
	BTreeFree(fixpointContext->calls);
	FreeResizingArray(&fixpointContext->pendingTuples);
	BTreeFree(fixpointContext->tuples);
	Free(fixpointContext->childArguments);
}


/**
 * Find the fixpoint operator context deriving the relation that a recurse operator
 * enumerates, being the nearest one enclosing it.
 */
static OperatorContext * findFixpointContext(OperatorContext * context)
{
	for(OperatorContext * ancestor = context->parent; ancestor; ancestor = ancestor->parent) {
		if(ancestor->op->type == OPERATOR_FIXPOINT)
			return ancestor;
	}
	return 0;
}


#ifdef DEBUG
/**
 * Verify that a recurse operator binds every argument the fixpoint operator does.
 * Call bindings are keyed on the fixpoint's input arguments, so a recursive term must
 * bind at least those to name the call it is making. It may bind more, which only
 * restricts the tuples it yields.
 *
 * A recursive term binding fewer of them asks for something broader than any call keyed
 * this way can express, and needs a table of its own binding pattern.
 */
static void assertCallBindingIsNamed(Operator const * recurse, Operator const * fixpoint)
{
	for(index8 i = 0; i < fixpoint->impl.fixpoint.nInputs; i++) {
		bool bound = false;
		for(index8 j = 0; j < recurse->impl.recurse.nInputs; j++)
			bound = bound
				|| (fixpoint->impl.fixpoint.inputArguments[i]
					== recurse->impl.recurse.inputArguments[j]);
		ASSERT(bound)
	}
}
#endif


static void recurseSetupContext(OperatorContext * context)
{
	RecurseContext * recurseContext = (RecurseContext *) &context->data;
	Operator const * op = context->op;
	OperatorContext * fixpointOperatorContext = findFixpointContext(context);
	// A recurse operator may only occur in the subtree of a fixpoint operator
	ASSERT(fixpointOperatorContext)
	Operator const * fixpointOperator = fixpointOperatorContext->op;
	ASSERT(fixpointOperator->nArguments == op->nArguments)
#ifdef DEBUG
	assertCallBindingIsNamed(op, fixpointOperator);
#endif
	FixpointContext * fixpointContext =
		(FixpointContext *) &fixpointOperatorContext->data;

	// Record the binding this term is asking for, so that a later round derives it.
	// The binding takes the fixpoint's input arguments, as that is what the derivation
	// is keyed on: a term binding more arguments than the derivation distinguishes asks
	// for a call already covered by a broader one, and only filters what it yields.
	Atom * binding = Allocate(op->nArguments * sizeof(Atom));
	setupCallBinding(
		binding, context->arguments,
		fixpointOperator->impl.fixpoint.inputArguments,
		fixpointOperator->impl.fixpoint.nInputs, op->nArguments);
	BTreeInsert(fixpointContext->pendingCalls, binding);
	Free(binding);

	BTreeIterate(&recurseContext->iterator, fixpointContext->tuples);
}


static bool recurseCall(OperatorContext * context)
{
	RecurseContext * recurseContext = (RecurseContext *) &context->data;
	return yieldDerivedTuple(
		context, &recurseContext->iterator,
		context->op->impl.recurse.inputArguments, context->op->impl.recurse.nInputs);
}


static void recurseFinalizeContext(OperatorContext * context)
{
	RecurseContext * recurseContext = (RecurseContext *) &context->data;
	BTreeIteratorEnd(&recurseContext->iterator);
}


//------------------------------------- OPERATOR_MACHINE -----------------------------------------


static void machineSetupContext(OperatorContext * context)
{
	MachineProvider * provider = context->op->impl.machine.provider;
	if(provider->setupContext)
		provider->setupContext(context);
}


static bool machineCall(OperatorContext * context)
{
	return context->op->impl.machine.provider->call(context);
}


static void machineFinalizeContext(OperatorContext * context)
{
	MachineProvider * provider = context->op->impl.machine.provider;
	if(provider->finalizeContext)
		provider->finalizeContext(context);
}


Operator * CreateMachineOperator(
	size8 nArguments, index8 const indexOrder[], MachineProvider * provider,
	void * providerData, size32 contextSize)
{
	Operator * op = createOperator(OPERATOR_MACHINE, nArguments, contextSize);
	op->impl.machine.provider = provider;
	op->impl.machine.providerData = providerData;
	// A provider yielding at most one tuple declares no order
	if(indexOrder) {
		allocateIndexOrder(op);
		CopyMemory(indexOrder, op->indexOrder, nArguments);
#ifdef DEBUG
		assertIsIndexOrder(op->indexOrder, nArguments);
#endif
	}
	return op;
}


static void teardownMachineOperator(Operator * op)
{
	MachineProvider * provider = op->impl.machine.provider;
	if(provider->finalizeOperator)
		provider->finalizeOperator(op);
}


//------------------------------------- Generic Operator -----------------------------------------


size8 OperatorNChildren(Operator const * op)
{
	switch(op->type) {
	case OPERATOR_JOIN:
	case OPERATOR_UNION:
		return 2;

	case OPERATOR_PERMUTE:
	case OPERATOR_PROJECT:
	case OPERATOR_CONSTRAIN:
	case OPERATOR_FIXPOINT:
	case OPERATOR_FILTER:
		return 1;

	case OPERATOR_MACHINE:
	case OPERATOR_RECURSE:
		return 0;

	default:
		ASSERT(false)
		return 0;
	}
}


Operator * OperatorGetChild(Operator const * op, index8 index)
{
	ASSERT(index < OperatorNChildren(op))
	switch(op->type) {
	case OPERATOR_PERMUTE:
		return op->impl.permute.childOperator;

	case OPERATOR_JOIN:
		return index ? op->impl.join.right : op->impl.join.left;

	case OPERATOR_UNION:
		return index ? op->impl._union.second : op->impl._union.first;

	case OPERATOR_PROJECT:
		return op->impl.project.childOperator;

	case OPERATOR_CONSTRAIN:
		return op->impl.constrain.childOperator;

	case OPERATOR_FILTER:
		return op->impl.filter.childOperator;

	case OPERATOR_FIXPOINT:
		return op->impl.fixpoint.childOperator;

	default:
		ASSERT(false)
		return 0;
	}
}


void AcquireOperator(Operator * op)
{
	op->referenceCount++;
}


void ReleaseOperator(Operator * op)
{
	op->referenceCount--;
	if(op->referenceCount == 0) {
		switch(op->type) {
		case OPERATOR_PERMUTE:
			teardownPermuteOperator(op);
			break;

		case OPERATOR_JOIN:
			teardownJoinOperator(op);
			break;

		case OPERATOR_UNION:
			teardownUnionOperator(op);
			break;

		case OPERATOR_PROJECT:
			teardownProjectOperator(op);
			break;

		case OPERATOR_CONSTRAIN:
			teardownConstrainOperator(op);
			break;

		case OPERATOR_FILTER:
			teardownFilterOperator(op);
			break;

		case OPERATOR_FIXPOINT:
			teardownFixpointOperator(op);
			break;

		case OPERATOR_RECURSE:
			teardownRecurseOperator(op);
			break;

		case OPERATOR_MACHINE:
			teardownMachineOperator(op);
			break;
		
		default:
			ASSERT(false)
			break;
		}
		if(op->indexOrder)
			Free(op->indexOrder);
		Free(op);
	}
}


static OperatorContext * createChildContext(
	OperatorContext * parent, Operator const * op, Atom arguments[])
{
	size32 contextSize = sizeof(OperatorContext) + op->contextSize;
	OperatorContext * context = Allocate(contextSize);
	SetMemory(context, contextSize, 0);
	context->op = op;
	context->arguments = arguments;
	context->parent = parent;

	switch(op->type) {
	case OPERATOR_PERMUTE:
		permuteSetupContext(context);
		break;

	case OPERATOR_JOIN:
		joinSetupContext(context);
		break;

	case OPERATOR_UNION:
		unionSetupContext(context);
		break;

	case OPERATOR_PROJECT:
		projectSetupContext(context);
		break;

	case OPERATOR_CONSTRAIN:
		constrainSetupContext(context);
		break;

	case OPERATOR_FILTER:
		filterSetupContext(context);
		break;

	case OPERATOR_FIXPOINT:
		fixpointSetupContext(context);
		break;

	case OPERATOR_RECURSE:
		recurseSetupContext(context);
		break;

	case OPERATOR_MACHINE:
		machineSetupContext(context);
		break;
	
	default:
		ASSERT(false)
		break;
	}
	return context;
}


OperatorContext * OperatorCreateContext(Operator const * op, Atom arguments[])
{
	return createChildContext(0, op, arguments);
}


#ifdef DEBUG
/**
 * Verify that the tuple just yielded ascends strictly with respect to the operator's
 * index order, which is the ordering contract every operator must uphold; see operator.h.
 * A machine operator failing this is a fault in its provider, which declared an order
 * it does not yield in.
 */
static void assertTupleAscends(OperatorContext * context)
{
	Operator const * op = context->op;
	if(!op->nArguments)
		return;
	if(context->previousTuple) {
		// An operator declaring no index order claims to yield at most one tuple
		ASSERT(op->indexOrder)
		ASSERT(TupleCompareInOrder(
			context->previousTuple, context->arguments, op->indexOrder, op->nArguments) < 0)
	}
	else
		context->previousTuple = Allocate(op->nArguments * sizeof(Atom));
	CopyMemory(context->arguments, context->previousTuple, op->nArguments * sizeof(Atom));
}
#endif


bool OperatorCall(OperatorContext * context)
{
	bool success;
	switch(context->op->type) {
	case OPERATOR_PERMUTE:
		success = permuteCall(context);
		break;

	case OPERATOR_JOIN:
		success = joinCall(context);
		break;

	case OPERATOR_UNION:
		success = unionCall(context);
		break;

	case OPERATOR_PROJECT:
		success = projectCall(context);
		break;

	case OPERATOR_CONSTRAIN:
		success = constrainCall(context);
		break;

	case OPERATOR_FILTER:
		success = filterCall(context);
		break;

	case OPERATOR_FIXPOINT:
		success = fixpointCall(context);
		break;

	case OPERATOR_RECURSE:
		success = recurseCall(context);
		break;

	case OPERATOR_MACHINE:
		success = machineCall(context);
		break;

	default:
		ASSERT(false)
		success = false;
		break;
	}
#ifdef DEBUG
	if(success)
		assertTupleAscends(context);
#endif
	return success;
}


void OperatorFreeContext(OperatorContext * context)
{
	switch(context->op->type) {
	case OPERATOR_PERMUTE:
		permuteFinalizeContext(context);
		break;

	case OPERATOR_JOIN:
		joinFinalizeContext(context);
		break;

	case OPERATOR_UNION:
		unionFinalizeContext(context);
		break;

	case OPERATOR_PROJECT:
		projectFinalizeContext(context);
		break;

	case OPERATOR_CONSTRAIN:
		constrainFinalizeContext(context);
		break;

	case OPERATOR_FILTER:
		filterFinalizeContext(context);
		break;

	case OPERATOR_FIXPOINT:
		fixpointFinalizeContext(context);
		break;

	case OPERATOR_RECURSE:
		recurseFinalizeContext(context);
		break;

	case OPERATOR_MACHINE:
		machineFinalizeContext(context);
		break;
	
	default:
		ASSERT(false)
		break;
	}
#ifdef DEBUG
	if(context->previousTuple)
		Free(context->previousTuple);
#endif
	Free(context);
}


bool OperatorCallOnce(Operator const * op, Atom arguments[])
{
	OperatorContext * context = OperatorCreateContext(op, arguments);
	bool result = OperatorCall(context);
	OperatorFreeContext(context);
	return result;
}


/**
 * Print the name and arity of an operator, followed by the order in which it yields
 * its tuples, as "JOIN/3[0 2 1]". An operator that declares no order, yielding at most
 * one tuple, prints an empty "[]".
 */
static void printOperatorHead(Operator const * op, char const * name)
{
	PrintF("%s/%u[", name, op->nArguments);
	if(op->indexOrder) {
		for(index8 i = 0; i < op->nArguments; i++) {
			if(i)
				PrintChar(' ');
			PrintF("%u", op->indexOrder[i]);
		}
	}
	PrintChar(']');
}


/**
 * Print the arguments a caller binds when calling a fixpoint or recurse operator, as
 * "<0 2>", following the convention that marks an input parameter of a service. An
 * operator binding none prints nothing.
 */
static void printInputArguments(index8 const inputArguments[], size8 nInputs)
{
	if(!nInputs)
		return;
	PrintChar('<');
	for(index8 i = 0; i < nInputs; i++) {
		if(i)
			PrintChar(' ');
		PrintF("%u", inputArguments[i]);
	}
	PrintChar('>');
}


void PrintOperator(Operator const * op)
{
	switch(op->type) {
	case OPERATOR_PERMUTE:
		printOperatorHead(op, "PERMUTE");
		PrintChar('(');
		for(index8 i = 0; i < op->impl.permute.childOperator->nArguments; i++)
			PrintF("%u ", op->impl.permute.argumentMap[i]);
		PrintChar('{');
		PrintTuple(
			op->impl.permute.constantTypes,
			op->impl.permute.constants,
			op->impl.permute.nConstants
		);
		PrintCString("} ");
		PrintOperator(op->impl.permute.childOperator);
		PrintChar(')');
		break;

	case OPERATOR_JOIN:
		printOperatorHead(op, "JOIN");
		PrintChar('(');
		for(index8 i = 0; i < op->impl.join.left->nArguments; i++)
			PrintF("%u ", op->impl.join.leftMap[i]);
		PrintOperator(op->impl.join.left);
		for(index8 i = 0; i < op->impl.join.right->nArguments; i++)
			PrintF("%u ", op->impl.join.rightMap[i]);
		PrintOperator(op->impl.join.right);
		PrintChar(')');
		break;

	case OPERATOR_UNION:
		printOperatorHead(op, "UNION");
		PrintChar('(');
		PrintOperator(op->impl._union.first);
		PrintOperator(op->impl._union.second);
		PrintChar(')');
		break;

	case OPERATOR_PROJECT:
		printOperatorHead(op, "PROJECT");
		PrintChar('(');
		for(index8 i = 0; i < op->nArguments; i++)
			PrintF("%u ", op->impl.project.argumentMap[i]);
		PrintOperator(op->impl.project.childOperator);
		PrintChar(')');
		break;

	case OPERATOR_CONSTRAIN:
		printOperatorHead(op, "CONSTRAIN");
		PrintChar('(');
		for(index8 i = 0; i < op->impl.constrain.childOperator->nArguments; i++)
			PrintF("%u ", op->impl.constrain.argumentMap[i]);
		PrintOperator(op->impl.constrain.childOperator);
		PrintChar(')');
		break;

	case OPERATOR_FILTER:
		printOperatorHead(op, "FILTER");
		printInputArguments(op->impl.filter.inputArguments, op->impl.filter.nInputs);
		PrintChar('(');
		PrintOperator(op->impl.filter.childOperator);
		PrintChar(')');
		break;

	case OPERATOR_FIXPOINT:
		printOperatorHead(op, "FIXPOINT");
		printInputArguments(
			op->impl.fixpoint.inputArguments, op->impl.fixpoint.nInputs);
		PrintChar('(');
		PrintOperator(op->impl.fixpoint.childOperator);
		PrintChar(')');
		break;

	case OPERATOR_RECURSE:
		printOperatorHead(op, "RECURSE");
		printInputArguments(
			op->impl.recurse.inputArguments, op->impl.recurse.nInputs);
		break;

	case OPERATOR_MACHINE:
		printOperatorHead(op, "MACHINE");
		break;

	default:
		ASSERT(false);
		break;
	}
}
