
#include "kernel/expression.h"
#include "memory/allocator.h"


/**
 * Setup the common part of an Expression
 */
static void setupExpression(
	Expression * expression, enum ExpressionType type, size8 nArguments, index8 const * argumentMap)
{
	SetMemory(expression, sizeof(Expression), 0);
	expression->type = type;
	ASSERT(nArguments <= 8);	// due to fixed argument map array size
	expression->dimensions.nArguments = nArguments;
	if(argumentMap)
		CopyMemory(argumentMap, &(expression->argumentMap), nArguments);
	else {
		for(index8 i = 0; i < nArguments; i++)
			expression->argumentMap[i] = i;
	}
}


void SetupMachineExpression(
	Expression * expression, size8 nArguments, index8 const * argumentMap, MachineService const * machineService)
{
	setupExpression(expression, EXPRESSION_MACHINE, nArguments, argumentMap);
	expression->dimensions.contextSize = machineService->contextSize;
	expression->value.machineService = *machineService;
}


typedef struct s_JoinContext {
	Tuple * argumentsCopy;
	ExpressionContext * leftContext;
	ExpressionContext * rightContext;
} JoinContext;


void SetupJoinExpression(
	Expression * expression, size8 nArguments, index8 const * argumentMap,
	Expression const * leftChild, Expression const * rightChild)
{
	setupExpression(expression, EXPRESSION_JOIN, nArguments, argumentMap);
	expression->dimensions.contextSize = sizeof(JoinContext);
	expression->value.children.left = leftChild;
	expression->value.children.right = rightChild;
}


/**
 * Obtain a tuple from the left child expression of a join expression
 * and setup the right child expression context for evaluation.
 */
static bool joinExpressionEvaluateLeft(ExpressionContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	// restore arguments tuple
	CopyTuples(joinContext->argumentsCopy, context->arguments);

	// Obtain next tuple from left expression, if any.
	// This will write directly to the context->arguments tuple
	ASSERT(joinContext->leftContext)
	if(!ExpressionCall(joinContext->leftContext)) {
		// no more tuples, free child context
		ExpressionFreeContext(joinContext->leftContext);
		joinContext->leftContext = 0;
		joinContext->rightContext = 0;
		return false;
	}
	// start new evaluation of right expression
	Expression const * right = context->expression->value.children.right;
	joinContext->rightContext = ExpressionCreateContext(right, context->arguments);
	return true;
}


static void joinExpressionSetupContext(ExpressionContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	// For the right context, we must create a new context for each tuple from the left,
	// and so we need to store a copy of the query tuple to prevent overwriting it.
	joinContext->argumentsCopy = CreateTupleFromTuple(context->arguments);
	// evaluate first left expression to prepare for iteration
	Expression const * left = context->expression->value.children.left;
	joinContext->leftContext = ExpressionCreateContext(left, context->arguments);
	joinExpressionEvaluateLeft(context);
}


static bool joinExpressionCall(ExpressionContext * context)
{
	/**
	 * Each call to a join expression gives one tuple from the Carthesian product
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

	// attempt to obtain next tuple from right expression
	while(!ExpressionCall(joinContext->rightContext)) {
		// no more tuples from right expression, start over with new left tuple
		ExpressionFreeContext(joinContext->rightContext);
		if(!joinExpressionEvaluateLeft(context)) {
			// join iteration is complete
			return false;
		}
	}
	// yield the resulting tuple
	return true;
}


static void joinExpressionFinalizeContext(ExpressionContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	if(joinContext->leftContext)
		ExpressionFreeContext(joinContext->leftContext);
	if(joinContext->rightContext)
		ExpressionFreeContext(joinContext->rightContext);
	FreeTuple(joinContext->argumentsCopy);
}

/**
 * Machine service expression
 */

static void machineServiceSetupContext(MachineService const * service, ExpressionContext * context)
{
	service->provider->setupContext(context, service->providerData);
}


static bool machineServiceCall(ExpressionContext * context)
{
	MachineService const * service = &context->expression->value.machineService;
	return service->provider->call(context);
}


static void machineServiceFinalizeContext(ExpressionContext * context)
{
	MachineService const * service = &context->expression->value.machineService;
	service->provider->finalizeContext(context);
}


ExpressionContext * ExpressionCreateContext(Expression const * expression, Tuple * arguments)
{
	size32 contextSize = sizeof(ExpressionContext) + expression->dimensions.contextSize;
	ExpressionContext * context = Allocate(contextSize);
	SetMemory(context, contextSize, 0);
	context->expression = expression;
	context->arguments = arguments;
	
	switch(expression->type) {
	case EXPRESSION_JOIN:
		joinExpressionSetupContext(context);
		break;

	case EXPRESSION_UNION:
		// TODO
		ASSERT(false)
		break;

	case EXPRESSION_PROJECT:
		// TODO
		ASSERT(false)
		break;

	case EXPRESSION_MACHINE:
		machineServiceSetupContext(&(expression->value.machineService), context);
		break;
	
	default:
		ASSERT(false)
		break;
	}
	return context;
}


bool ExpressionCall(ExpressionContext * context)
{
	switch(context->expression->type) {
	case EXPRESSION_JOIN:
		return joinExpressionCall(context);

	case EXPRESSION_UNION:
		// TODO
		ASSERT(false)
		return false;

	case EXPRESSION_PROJECT:
		// TODO
		ASSERT(false)
		return false;

	case EXPRESSION_MACHINE:
		return machineServiceCall(context);
	
	default:
		ASSERT(false)
		return false;
	}
}


void ExpressionFreeContext(ExpressionContext * context)
{
	switch(context->expression->type) {
	case EXPRESSION_JOIN:
		joinExpressionFinalizeContext(context);
		break;

	case EXPRESSION_UNION:
		// TODO
		ASSERT(false)
		break;

	case EXPRESSION_PROJECT:
		// TODO
		ASSERT(false)
		break;

	case EXPRESSION_MACHINE:
		machineServiceFinalizeContext(context);
		break;
	
	default:
		ASSERT(false)
		break;
	}
	Free(context);
}


void PrintExpression(Expression const * expression)
{
	switch(expression->type) {
	case EXPRESSION_JOIN:
		PrintCString("JOIN");
		break;
	case EXPRESSION_UNION:
		PrintCString("UNION");
		break;
	case EXPRESSION_PROJECT:
		PrintCString("PROJECT");
		break;
	case EXPRESSION_MACHINE:
		PrintCString("MACHINE");
		break;
	default:
		ASSERT(false);
		break;
	}
}


TypedAtom ExpressionContextReadArgument(ExpressionContext * context, index8 index)
{
	index8 const * argumentMap = context->expression->argumentMap;
	return TupleGetElement(context->arguments, argumentMap[index]);
}


void ExpressionContextWriteArgument(ExpressionContext * context, index8 index, TypedAtom argument)
{
	index8 const * argumentMap = context->expression->argumentMap;
	TupleSetElement(context->arguments, argumentMap[index], argument);
}
