/**
 * An expression, intermediate representation.
 * This is essentially relation algebra operators.
 * An expression is evaluated stepwise, at each call
 * yielding one tuple, similar to a co-routine.
 * 
 * NOTE: this should perhaps be renamed "operator" as it encodes
 * stepwise operations to be executed by the interpreter?
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
		// Length of arguments tuple for this expression
		size32 nArguments:8;
		// Context size, in addition to sizeof(ExpressionContext)
		size32 contextSize:24;
	} dimensions;
	// Indices into argument tuple
	// Fixed size for now; need to figure out allocation
	index8 argumentMap[8];
	union {
		// for EXPRESSION_JOIN
		struct {
			Expression const * left;
			Expression const * right; 
		} children;
		// for EXPRESSION_MACHINE (leaves)
		MachineService machineService;
	} value;
};

/**
 * Create a "leaf" expression representing a machine service call
 */
void SetupMachineExpression(
	Expression * expression, size8 nArguments, index8 const * argumentMap, MachineService const * machineService);

/**
 * Create a join expression from two existing expressions
 */
void SetupJoinExpression(
	Expression * expression, size8 nArguments, index8 const * argumentMap,
	Expression const * leftChild, Expression const * rightChild);


/**
 * Evaluating an expression consists of setting up an execution context,
 * performing one or more calls against that context, and finalizing the context.
 * Sub-expressions will have their own execution contexts, which are initialized
 * as necessary.
 */

typedef struct s_ExpressionContext {
	Expression const * expression;
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
bool ExpressionCall(ExpressionContext * context);

/**
 * Finalize an expression context, releasing any allocated resources.
 */
void ExpressionFreeContext(ExpressionContext * context);

/**
 * Read and write context arguments
 */
TypedAtom ExpressionContextReadArgument(ExpressionContext * context, index8 index);

void ExpressionContextWriteArgument(ExpressionContext * context, index8 index, TypedAtom argument);


void PrintExpression(Expression const * expression);


#endif	// EXPRESSION_H
