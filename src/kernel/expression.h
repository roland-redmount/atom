/**
 * A description of a service as a hierarchy of expressions.
 * 
 * It is constructed from rule dispatch
 * and can be used to generate bytecode, serving as an intermediate
 * representation.
 */

#ifndef EXPRESSION_H
#define EXPRESSION_H

#include "kernel/ServiceRegistry.h"

 enum ExpressionType {
	CALL_EXPRESSION = 1,	// B-tree &c
	JOIN_EXPRESSION = 2,
	UNION_EXPRESSION = 3,
};

 
typedef struct s_Expression Expression;

struct s_Expression {
size8 nArguments;
	/*
	 * Mapping between parameters of this expression
	 * and parameters of the dependent expression.
	 */
	index8 * argumentMap;

	enum ExpressionType type;
	union {
		Expression * joinExpressions;
		ServiceRecord record;
	} fields;
};


typedef struct s_EvaluationContext {
	Expression const * expression;
	Tuple * arguments;
	index8 * argumentMap;
	union {
		RelationBTreeIterator btreeIterator;
	} fields;
} EvaluationContext;


/**
 * Iterate over an expression with a given argument tuple.
 * This is a form of co-routine call, similar to other iterators.
 * Might even make bytecode evaluation unnecessary?
 * 
 * The context stores the evaluation-dependent information,
 * such as iterator state for a B-tree iterator, &c
 * 
 * The argumentMap vector maps arguments from caller to the expression,
 * avoiding the need to copy tuples between services in the expression hierarchy.
 * This vector would typically be defined by unification.
 */
void ExpressionIterate(Expression const * expression, Tuple * arguments, index8 * argumentMap, EvaluationContext * context);

/**
 * Resume evaluating the expression, return true if a tuple was produced,
 * false if evaluation terminated.
 * If true, values are written to the arguments tuple supplied to ExpressionIterate()
 */
bool ExpressionNext(EvaluationContext * context);

/**
 * Check if the evaluation has yielded a tuple.
 */
void ExpressionHasTuple(EvaluationContext * context);

/**
 * Terminate evaluation
 */
void ExpressionEnd(EvaluationContext * context);


#endif		// EXPRESSION_H
