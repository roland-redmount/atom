/**
 * An expression, intermediate representation.
 * This is essentially relation algebra operators.
 */

 #ifndef EXPRESSION_H
 #define EXPRESSION_H

#include "kernel/machineservice.h"
 
 enum ExpressionType {
	EXPRESSION_JOIN,
	EXPRESSION_UNION,
	EXPRESSION_PROJECT,
	EXPRESSION_MACHINE,
};


/**
 * An expression can be executed by the interpreter to provide
 * a service. An expression can consist of sub-expressions, forming
 * a tree. The top level expression is associated with service of
 * type SERVICE_EXPRESSION. The leaves of an expression tree refer to
 * a machine service.
 */

typedef struct s_Expression Expression;

struct s_Expression {
	enum ExpressionType type;
	struct {
		size32 nArguments:8;
		size32 contextSize:24;
	} dimensions;
	union {
		// for EXPRESSION_JOIN, _UNION, _PROJECT (internal nodes)
		struct {
			Expression const * left;
			index8 leftArgumentMap[8];		// fixes size for now; need to figure out allocation
			Expression const * right; 
			index8 rightArgumentMap[8];
		} children;
		// for EXPRESSION_MACHINE (leaves)
		MachineService machineService;
	} value;
};

/**
 * Create a "leaf" expression representing a machine service call
 */
void CreateMachineExpression(Expression * expression, size8 nArguments, MachineService * machineService);

/**
 * Create a join expression from two existing expression.
 */
void CreateJoinExpression(Expression * expression, size8 nArguments,
	Expression const * leftChild, index8 * leftArgumentMap,
	Expression const * rightChild, index8 * rightArgumentMap);


/**
 * Evaluating an expression consists of setting up an execution context,
 * performing one or more calls against that context, and finalizing the context.
 * Sub-expressions will have their own execution contexts, which are initialized
 * as necessary.
 */

typedef struct s_ExpressionContext {
	Tuple * arguments;
	byte data[];
} ExpressionContext;


 /**
  * Create and return an execution context for evaluating an expression
  * with the given argument tuple. Each ExpressionCall() to this context
  * will yield result into the given tuple.
  */
ExpressionContext * ExpressionCreateContext(Expression const * expression, Tuple * arguments);

bool ExpressionCall(Expression const * expression, ExpressionContext * context);

void ExpressionFreeContext(Expression const * expression, ExpressionContext * context);

void PrintExpression(Expression const * expression);

/*
	// Mapping between arguments of this service
	// and parameters of each sub-service.
	// TODO: for now we use a fixed maximum number of arguments
	index8 leftArgumentMap[8];
	index8 rightArgumentMap[8];
	Atom leftService;
	Atom rightService;
*/

#endif	// EXPRESSION_H
