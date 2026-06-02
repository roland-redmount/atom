/**
 * An expression, intermediate representation.
 * This is essentially relation algebra operators.
 * An expression is evaluated stepwise, at each call
 * yielding one tuple, similar to a co-routine.
 * 
 * NOTE: this should perhaps be renamed "operator" as it encodes
 * stepwise operations to be executed by the interpreter.
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
 * a tree. The "leaves" of this tree are always EXPRESSION_MACHINE.
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

/**
 * Evaluate an expression with a given context. This is the interpreter.
 */
bool ExpressionCall(Expression const * expression, ExpressionContext * context);

void ExpressionFreeContext(Expression const * expression, ExpressionContext * context);

void PrintExpression(Expression const * expression);


#endif	// EXPRESSION_H
