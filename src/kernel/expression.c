
#include "kernel/expression.h"
#include "memory/allocator.h"


void CreateMachineExpression(Expression * expression, size8 nArguments, MachineService * machineService)
{
	SetMemory(expression, sizeof(Expression), 0);
	expression->type = EXPRESSION_MACHINE;
	expression->dimensions.nArguments = nArguments;
	expression->dimensions.contextSize = machineService->contextSize;
	expression->value.machineService = *machineService;
}


typedef struct s_JoinContext {
	Tuple * arguments;
	void * leftContext;
	Tuple * leftArguments;
	Tuple * rightQuery;
	void * rightContext;
	Tuple * rightArguments;
} JoinContext;


void CreateJoinExpression(Expression * expression, size8 nArguments,
	Expression const * leftChild, index8 * leftArgumentMap,
	Expression const * rightChild, index8 * rightArgumentMap)
{
	expression->type = EXPRESSION_JOIN;
	expression->dimensions.nArguments = nArguments;
	expression->dimensions.contextSize = sizeof(JoinContext);
	expression->value.children.left = leftChild;
	expression->value.children.right = rightChild;
	CopyMemory(
		leftArgumentMap,
		&(expression->value.children.leftArgumentMap),
		leftChild->dimensions.nArguments);
	CopyMemory(
		rightArgumentMap,
		&(expression->value.children.rightArgumentMap),
		rightChild->dimensions.nArguments
	);
}


/**
 * Obtain a tuple from the left child expression of a join expression
 * and setup the right child expression context for evaluation.
 */
static bool joinExpressionEvaluateLeft(Expression const * expression, ExpressionContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	TupleClear(context->arguments);

	// obtain next tuple from left expression (if any)
	Expression const * left = expression->value.children.left;
	ASSERT(joinContext->leftContext)
	if(!ExpressionCall(left, joinContext->leftContext)) {
		ExpressionFreeContext(expression->value.children.left, joinContext->leftContext);
		joinContext->leftContext = 0;
		joinContext->rightContext = 0;
		return false;
	}
	// copy values from left expression to join expression arguments
	for(index8 i = 0; i < left->dimensions.nArguments; i++) {
		TupleSetElement(
			context->arguments,
			expression->value.children.leftArgumentMap[i],
			TupleGetElement(joinContext->leftArguments, i)
		);
	}
	// copy determined values to right expression arguments
	Expression const * right = expression->value.children.right;
	for(index8 i = 0; i < right->dimensions.nArguments; i++) {
		TypedAtom typedAtom = TupleGetElement(
			context->arguments, expression->value.children.rightArgumentMap[i]);
		if(typedAtom.atom) {
			// atom was determined from the left expression, substitute it
			// into the right expression arguments (equality constraint)
			TupleSetElement(joinContext->rightArguments, i, typedAtom);
		}
		else {
			// use the user-supplied argument 
			TupleSetElement(joinContext->rightArguments, i,
				TupleGetElement(joinContext->rightQuery, i));
		}
	}
	// start new evaluation of right expression
	joinContext->rightContext = ExpressionCreateContext(right, joinContext->rightArguments);
	return true;
}


static void joinExpressionSetupContext(Expression const * expression, ExpressionContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	Expression const * left = expression->value.children.left;
	Expression const * right = expression->value.children.right;

	// For the left expression we create the context once
	joinContext->leftArguments = CreateTuple(left->dimensions.nArguments);
	for(index8 i = 0; i < left->dimensions.nArguments; i++) {
		TupleSetElement(joinContext->leftArguments, i,
			TupleGetElement(context->arguments, expression->value.children.leftArgumentMap[i])
		);
	}
	// For the right context, we must create a new context for each tuple from the left,
	// and so we need to store a copy of the query tuple to prevent overwriting it.
	joinContext->rightQuery = CreateTuple(right->dimensions.nArguments);
	for(index8 i = 0; i < right->dimensions.nArguments; i++) {
		TupleSetElement(joinContext->rightQuery, i,
			TupleGetElement(context->arguments, expression->value.children.rightArgumentMap[i])
		);
	}
	joinContext->rightArguments = CreateTuple(right->dimensions.nArguments);

	// evaluate first left expression to prepare for iteration
	joinContext->leftContext = ExpressionCreateContext(left, joinContext->leftArguments);
	joinExpressionEvaluateLeft(expression, context);
}


static bool joinExpressionCall(Expression const * expression, ExpressionContext * context)
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
	Expression const * right = expression->value.children.right;
	while(!ExpressionCall(right, joinContext->rightContext)) {
		// no more tuples from right expression, start over with new left tuple
		ExpressionFreeContext(right, joinContext->rightContext);
		if(!joinExpressionEvaluateLeft(expression, context)) {
			// join iteration is complete
			return false;
		}
	}
	// copy results from right expression
	for(index8 i = 0; i < right->dimensions.nArguments; i++) {
		TupleSetElement(
			context->arguments,
			expression->value.children.rightArgumentMap[i],
			TupleGetElement(joinContext->rightArguments, i)
		);
	}
	// yield the resulting tuple
	return true;
}


static void joinExpressionFreeContext(Expression const * expression, ExpressionContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	if(joinContext->leftContext)
		ExpressionFreeContext(expression->value.children.left, joinContext->leftContext);
	if(joinContext->rightContext)
		ExpressionFreeContext(expression->value.children.right, joinContext->rightContext);
	FreeTuple(joinContext->leftArguments);
	FreeTuple(joinContext->rightQuery);
	FreeTuple(joinContext->rightArguments);
}

/**
 * Machine service expression
 */

static void machineServiceSetupContext(MachineService const * service, ExpressionContext * context, Tuple * arguments)
{
	service->provider->setupContext(
		&context->data,
		service->providerData,
		arguments
	);
}


static bool machineServiceCall(MachineService const * service, ExpressionContext * context)
{
	return service->provider->call(&context->data, context->arguments);
}


static void machineServiceFreeContext(MachineService const * service, ExpressionContext * context)
{
	service->provider->freeContext(&context->data);
}


ExpressionContext * ExpressionCreateContext(Expression const * expression, Tuple * arguments)
{
	size32 contextSize = sizeof(ExpressionContext) + expression->dimensions.contextSize;
	ExpressionContext * context = Allocate(contextSize);
	SetMemory(context, contextSize, 0);
	context->arguments = arguments;
	
	switch(expression->type) {
	case EXPRESSION_JOIN:
		joinExpressionSetupContext(expression, context);
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
		machineServiceSetupContext(&(expression->value.machineService), context, arguments);
		break;
	
	default:
		ASSERT(false)
		break;
	}
	return context;
}


bool ExpressionCall(Expression const * expression, ExpressionContext * context)
{
	switch(expression->type) {
	case EXPRESSION_JOIN:
		return joinExpressionCall(expression, context);

	case EXPRESSION_UNION:
		// TODO
		ASSERT(false)
		return false;

	case EXPRESSION_PROJECT:
		// TODO
		ASSERT(false)
		return false;

	case EXPRESSION_MACHINE:
		return machineServiceCall(&(expression->value.machineService), context);
	
	default:
		ASSERT(false)
		return false;
	}
}


void ExpressionFreeContext(Expression const * expression, ExpressionContext * context)
{
	switch(expression->type) {
	case EXPRESSION_JOIN:
		joinExpressionFreeContext(expression, context);
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
		machineServiceFreeContext(&(expression->value.machineService), context);
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


