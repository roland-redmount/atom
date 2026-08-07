
#include "btree/btree.h"
#include "kernel/operator.h"
#include "kernel/tuple.h"
#include "memory/allocator.h"
#include "util/utilities.h"


/**
 * Create an Operator and setup common fields.
 * The caller obtains a reference to the created operator.
 */
static Operator * createOperator(enum OperatorType type, size8 nArguments, size32 contextSize)
{
	Operator * op = Allocate(sizeof(Operator));
	SetMemory(op, sizeof(Operator), 0);
	op->type = type;
	op->nArguments = nArguments;
	op->referenceCount = 1;
	op->contextSize = contextSize;
	return op;
}

/**
 * Copy parent arguments to a child arguments tuple, as given by an argument map.
 */
static void scatterArguments(
	Atom const * arguments, Atom * childArguments, index8 const * argumentMap, size8 nChildArguments)
{
	for(index8 i = 0; i < nChildArguments; i++)
		childArguments[i] = arguments[argumentMap[i]];
}


/**
 * Copy a child arguments tuple back to the parent arguments, as given by an argument map.
 */
static void gatherArguments(
	Atom * arguments, Atom const * childArguments, index8 const * argumentMap, size8 nChildArguments)
{
	for(index8 i = 0; i < nChildArguments; i++)
		arguments[argumentMap[i]] = childArguments[i];
}


#ifdef DEBUG
/**
 * Verify that no two child arguments take the same argument of an operator, ignoring
 * child arguments taken from elsewhere (the constants of a permute operator).
 * Mapping several child arguments to one argument is what a constrain operator
 * expresses, and no other operator does so.
 */
static void assertArgumentsAreDistinct(
	index8 const * argumentMap, size8 nChildArguments, size8 nArguments)
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
	size8 nArguments, Atom const * constants, byte const * constantTypes, size8 nConstants,
	index8 const * argumentMap, Operator * childOperator)
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
	permuteContext->childContext = OperatorCreateContext(
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
	size8 nArguments, index8 const * argumentMap, Operator * childOperator)
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

	constrainContext->childContext = OperatorCreateContext(
		op->impl.constrain.childOperator,
		constrainContext->childArguments
	);
}


/**
 * Test whether the child arguments taken from the same parent argument are equal.
 */
static bool constrainedArgumentsAgree(
	Atom const * childArguments, index8 const * argumentMap, size8 nChildArguments)
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
	index8 const * argumentMap, size8 nChildArguments, size8 nArguments)
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
	Operator * leftChild, index8 const * leftMap,
	Operator * rightChild, index8 const * rightMap)
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
	joinContext->rightContext = OperatorCreateContext(
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
	joinContext->leftContext = OperatorCreateContext(
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
	unionContext->lookaheadContext = OperatorCreateContext(
		context->op->impl._union.first, context->arguments);
	unionContext->nextContext = OperatorCreateContext(
		context->op->impl._union.second, context->arguments);
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


static bool unionCall(OperatorContext * context)
{
	UnionContext * unionContext = (UnionContext *) &context->data;
	size8 nArguments = context->op->nArguments;
	// We interleave tuples provided by the two operators to maintain sorted order,
	// and skip any identical tuples. At each call, we have a "lookahead" tuple from
	// one of the child operators, and we try to obtain a new tuple from the other
	// operator. If one exists, we compare it to the lookahead tuple and return the'
	// preceding one; else we have exhausted one operator.
	if(!unionContext->lookaheadContext) {
		// no lookahead operator, call other operator directly
		return OperatorCall(unionContext->nextContext);
	}
	
	if(!OperatorCall(unionContext->nextContext)) {
		// no more lookahead, swap contexts 
		OperatorFreeContext(unionContext->nextContext);
		unionContext->nextContext = 0;
		swapContexts(unionContext);
		CopyMemory(unionContext->lookahead, context->arguments, nArguments * sizeof(Atom));
		return true;
	}

	// Compare the newly obtained arguments tuple with the lookahead tuple 
	int8 order = TupleCompare(context->arguments, unionContext->lookahead, nArguments);
	if(order == 0) {
		// Duplicate tuple, acquire next (only one tuple can be equal)
		if(OperatorCall(unionContext->nextContext)) {
			order = TupleCompare(context->arguments, unionContext->lookahead, nArguments);
			ASSERT(order > 0)
		}
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
 * PROJECT keeps the first nArguments arguments of its child operator and drops the rest.
 * Dropping arguments may leave duplicate tuples, so we enumerate the entire child relation
 * into a B-tree keyed on the kept arguments, which both removes duplicates and orders the
 * result. As the kept arguments are a prefix of the child arguments tuple, the B-tree key
 * is simply the leading part of that tuple and needs no rearranging.
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

	Operator * childOperator = context->op->impl.project.childOperator;
	size8 nArguments = context->op->nArguments;
	size8 nChildArguments = childOperator->nArguments;

	// The child arguments tuple takes the caller's input arguments in its leading
	// positions; the dropped arguments are left unbound so the child enumerates them.
	Atom * childArguments = Allocate(nChildArguments * sizeof(Atom));
	CopyMemory(context->arguments, childArguments, nArguments * sizeof(Atom));
	SetMemory(&childArguments[nArguments], (nChildArguments - nArguments) * sizeof(Atom), 0);

	OperatorContext * childContext = OperatorCreateContext(childOperator, childArguments);
	// Retrieve all tuples from the child relation, keeping the leading arguments only
	projectContext->btree = BTreeCreate(
		nArguments * sizeof(Atom),
		btreeCompareTuples,
		0
	);
	while(OperatorCall(childContext)) {
		BTreeInsert(projectContext->btree, childArguments);
	}
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
}


Operator * CreateProjectOperator(Operator * childOperator, size8 nArguments)
{
	ASSERT(nArguments < childOperator->nArguments)
	Operator * op = createOperator(OPERATOR_PROJECT, nArguments, sizeof(ProjectContext));
	op->impl.project.childOperator = childOperator;
	AcquireOperator(childOperator);
	return op;
}


//------------------------------------- OPERATOR_MACHINE -----------------------------------------


static void machineSetupContext(OperatorContext * context)
{
	context->op->impl.machine.provider->setupContext(context);
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


Operator * CreateMachineOperator(size8 nArguments, MachineProvider * provider, void * providerData)
{
	Operator * op = createOperator(OPERATOR_MACHINE, nArguments, provider->contextSize);
	op->impl.machine.provider = provider;
	op->impl.machine.providerData = providerData;
	return op;
}


static void teardownMachineOperator(Operator * op)
{
	MachineProvider * provider = op->impl.machine.provider;
	if(provider->finalizeOperator)
		provider->finalizeOperator(op);
}


//------------------------------------- Generic Operator -----------------------------------------


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

		case OPERATOR_MACHINE:
			teardownMachineOperator(op);
			break;
		
		default:
			ASSERT(false)
			break;
		}
		Free(op);
	}
}


OperatorContext * OperatorCreateContext(Operator const * op, Atom arguments[])
{
	size32 contextSize = sizeof(OperatorContext) + op->contextSize;
	OperatorContext * context = Allocate(contextSize);
	SetMemory(context, contextSize, 0);
	context->op = op;
	context->arguments = arguments;
	
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

	case OPERATOR_MACHINE:
		machineSetupContext(context);
		break;
	
	default:
		ASSERT(false)
		break;
	}
	return context;
}


bool OperatorCall(OperatorContext * context)
{
	switch(context->op->type) {
	case OPERATOR_PERMUTE:
		return permuteCall(context);

	case OPERATOR_JOIN:
		return joinCall(context);

	case OPERATOR_UNION:
		return unionCall(context);

	case OPERATOR_PROJECT:
		return projectCall(context);

	case OPERATOR_CONSTRAIN:
		return constrainCall(context);

	case OPERATOR_MACHINE:
		return machineCall(context);
	
	default:
		ASSERT(false)
		return false;
	}
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

	case OPERATOR_MACHINE:
		machineFinalizeContext(context);
		break;
	
	default:
		ASSERT(false)
		break;
	}
	Free(context);
}


bool OperatorCallOnce(Operator const * op, Atom arguments[])
{
	OperatorContext * context = OperatorCreateContext(op, arguments);
	bool result = OperatorCall(context);
	OperatorFreeContext(context);
	return result;
}


void PrintOperator(Operator const * op)
{
	switch(op->type) {
	case OPERATOR_PERMUTE:
		PrintF("PERMUTE/%u(", op->nArguments);
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
		PrintF("JOIN/%u(", op->nArguments);
		for(index8 i = 0; i < op->impl.join.left->nArguments; i++)
			PrintF("%u ", op->impl.join.leftMap[i]);
		PrintOperator(op->impl.join.left);
		for(index8 i = 0; i < op->impl.join.right->nArguments; i++)
			PrintF("%u ", op->impl.join.rightMap[i]);
		PrintOperator(op->impl.join.right);
		PrintChar(')');
		break;

	case OPERATOR_UNION:
		PrintF("UNION/%u(", op->nArguments);
		PrintOperator(op->impl._union.first);
		PrintOperator(op->impl._union.second);
		PrintChar(')');
		break;

	case OPERATOR_PROJECT:
		PrintF("PROJECT/%u(", op->nArguments);
		PrintOperator(op->impl.project.childOperator);
		PrintChar(')');
		break;

	case OPERATOR_CONSTRAIN:
		PrintF("CONSTRAIN/%u(", op->nArguments);
		for(index8 i = 0; i < op->impl.constrain.childOperator->nArguments; i++)
			PrintF("%u ", op->impl.constrain.argumentMap[i]);
		PrintOperator(op->impl.constrain.childOperator);
		PrintChar(')');
		break;

	case OPERATOR_MACHINE:
		PrintF("MACHINE/%u", op->nArguments);
		break;

	default:
		ASSERT(false);
		break;
	}
}
