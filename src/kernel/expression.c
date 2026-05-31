
#include "kernel/expression.h"
#include "memory/allocator.h"


void CreateMachineExpression(Expression * expression, size8 nArguments, MachineService * machineService)
{
	SetMemory(expression, sizeof(Expression), 0);
	expression->type = EXPRESSION_MACHINE;
	expression->nArguments = nArguments;
	expression->value.machineService = *machineService;
}


void CreateJoinExpression(Expression * expression, size8 nArguments,
	Expression const * leftChild, index8 * leftArgumentMap,
	Expression const * rightChild, index8 * rightArgumentMap)
{
	expression->type = EXPRESSION_JOIN;
	expression->nArguments = nArguments;
	expression->value.children.left = leftChild;
	expression->value.children.right = rightChild;
	CopyMemory(leftArgumentMap, &(expression->value.children.leftArgumentMap), leftChild->nArguments);
	CopyMemory(rightArgumentMap, &(expression->value.children.rightArgumentMap), rightChild->nArguments);
}


typedef struct s_JoinContext {
	void * leftContext;
	Tuple * leftArguments;
	Tuple * leftResult;
	void * rightContext;
	Tuple * rightArguments;
	Tuple * rightResult;
	Tuple * joinResult;
} JoinContext;


/**
 * Obtain a tuple from the left child expression of a join expression
 * and setup the right child expression for evaluation.
 */
static bool joinExpressionEvaluateLeft(Expression const * expression, JoinContext * context)
{
	Expression const * left = expression->value.children.left;
	Expression const * right = expression->value.children.right;
	ASSERT(context->leftContext)

	// obtain next tuple from left expression (if any)
	if(!ExpressionCall(left, context->leftContext, context->leftResult)) {
		ExpressionFreeContext(expression->value.children.left, context->leftContext);
		context->leftContext = 0;
		context->rightContext = 0;
		return false;
	}
	// copy values of left result to join result
	TupleClear(context->joinResult);
	for(index8 i = 0; i < left->nArguments; i++) {
		TupleSetElement(context->joinResult, expression->value.children.leftArgumentMap[i],
			TupleGetElement(context->leftResult, i)
		);
	}
	// copy determined values of join result to right query
	for(index8 i = 0; i < right->nArguments; i++) {
		TypedAtom typedAtom = TupleGetElement(
			context->joinResult, expression->value.children.rightArgumentMap[i]);
		if(typedAtom.atom) {
			// atom was determined from the left expression, substitute it
			// in the right expression arguments (equality constraint)
			TupleSetElement(context->rightArguments, i, typedAtom);
		}
		// else use the supplied argument (already in place)
	}
	// start new evaluation of right expression
	context->rightContext = ExpressionCreateContext(right, context->rightArguments);
	return true;
}


static void * joinExpressionCreateContext(Expression const * expression, Tuple * arguments)
{
	JoinContext * context = Allocate(sizeof(JoinContext));
	SetMemory(context, sizeof(JoinContext), 0);
	Expression const * left = expression->value.children.left;
	Expression const * right = expression->value.children.right;

	context->leftArguments = CreateTuple(left->nArguments);
	for(index8 i = 0; i < left->nArguments; i++) {
		TupleSetElement(context->leftArguments, i,
			TupleGetElement(arguments, expression->value.children.leftArgumentMap[i])
		);
	}
	context->rightArguments = CreateTuple(right->nArguments);
	for(index8 i = 0; i < right->nArguments; i++) {
		TupleSetElement(context->rightArguments, i,
			TupleGetElement(arguments, expression->value.children.rightArgumentMap[i])
		);
	}

	context->leftResult = CreateTuple(left->nArguments);
	context->rightResult = CreateTuple(right->nArguments);
	context->joinResult = CreateTuple(expression->nArguments);

	// evaluate first left expression to prepare for iteration
	context->leftContext = ExpressionCreateContext(left, context->leftArguments);
	joinExpressionEvaluateLeft(expression, context);

	return context;
}


static bool joinExpressionCall(Expression const * expression, void * _context,  Tuple * result)
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
	JoinContext * context = _context;
	if(!context->leftContext)
		return false;

	// attempt to obtain next tuple from right expression
	while(!ExpressionCall(expression->value.children.right, context->rightContext, context->rightResult)) {
		// no more tuples from right expression, start over with new left tuple
		ExpressionFreeContext(expression->value.children.right, context->rightContext);
		if(!joinExpressionEvaluateLeft(expression, context)) {
			// join iteration is complete
			return false;
		}
	}
	// copy results from right expression
	for(index8 i = 0; i < expression->value.children.right->nArguments; i++) {
		TupleSetElement(
			context->joinResult,
			expression->value.children.rightArgumentMap[i],
			TupleGetElement(context->rightResult, i)
		);
	}
	// yield the resulting tuple
	CopyTuples(context->joinResult, result);
	return true;
}


static void joinExpressionFreeContext(Expression const * expression, void * _context)
{
	JoinContext * context = _context;
	if(context->leftContext)
		ExpressionFreeContext(expression->value.children.left, context->leftContext);
	if(context->rightContext)
		ExpressionFreeContext(expression->value.children.right, context->rightContext);
	FreeTuple(context->leftArguments);
	FreeTuple(context->rightArguments);
	FreeTuple(context->leftResult);
	FreeTuple(context->rightResult);
	FreeTuple(context->joinResult);
	Free(context);
}


void * ExpressionCreateContext(Expression const * expression, Tuple * arguments)
{
	switch(expression->type) {
	case EXPRESSION_JOIN:
		return joinExpressionCreateContext(expression, arguments);

	case EXPRESSION_UNION:
		// TODO
		ASSERT(false)
		return 0;

	case EXPRESSION_PROJECT:
		// TODO
		ASSERT(false)
		return 0;

	case EXPRESSION_MACHINE:
		return MachineServiceCreateContext(&(expression->value.machineService), arguments);
	
	default:
		ASSERT(false)
		return 0;
	}	
}


bool ExpressionCall(Expression const * expression, void * context,  Tuple * result)
{
	switch(expression->type) {
	case EXPRESSION_JOIN:
		return joinExpressionCall(expression, context, result);

	case EXPRESSION_UNION:
		// TODO
		ASSERT(false)
		return false;

	case EXPRESSION_PROJECT:
		// TODO
		ASSERT(false)
		return false;

	case EXPRESSION_MACHINE:
		return MachineServiceCall(&(expression->value.machineService), context, result);
	
	default:
		ASSERT(false)
		return false;
	}
}


void ExpressionFreeContext(Expression const * expression, void * context)
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
		MachineServiceFreeContext(&(expression->value.machineService), context);
		break;
	
	default:
		ASSERT(false)
		break;
	}
}


void PrintExpression(Expression const * expression)
{
	switch(expression->type) {
	case EXPRESSION_JOIN:
		PrintCString("JOIN");
	case EXPRESSION_UNION:
		PrintCString("UNION");
	case EXPRESSION_PROJECT:
		PrintCString("PROJECT");
	case EXPRESSION_MACHINE:
		PrintCString("MACHINE");
	}
}


