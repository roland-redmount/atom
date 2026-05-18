
#include "kernel/service.h"
// #include "kernel/RelationBTree.h"

/*

void ExpressionIterate(Expression const * expression, Tuple * arguments, index8 * permutation, EvaluationContext * context)
{
	context->expression = expression;
	context->arguments = arguments;
	context->permutation = permutation;

	switch(expression->type) {
	case CALL_EXPRESSION:
		// B-tree is the only call expression for now
		// TODO: this needs to take a permutation vector
		ASSERT(false)
		// RelationBTreeIterate(
		// 	expression->fields.record.provider.tree,
		// 	arguments,
		// 	&(context->fields.btreeIterator)
		// );
		break;

	default:
		ASSERT(false)
		break;
	}

}


bool ExpressionNext(EvaluationContext * context)
{
	switch(context->expression->type) {
	case CALL_EXPRESSION:
		// B-tree is the only call expression for now
		ASSERT(false)
		break;

	default:
		ASSERT(false)
		break;
	}
	return false;
}


void ExpressionEnd(EvaluationContext * context)
{
	ASSERT(false)
}

*/