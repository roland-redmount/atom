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
	size32 nChildren;
	union {
		Expression const * children;		// for JOIN, UNION, PROJECT
		MachineService machineService;		// for leaves
	} value;
	// argumentMap contains the position in this expression's
	// argument array corresponding to each argument in each child,
	// concatenated into a single array.
	index8 * argumentMap;
};

/**
 * Create a "leaf" expression representing a machine service call
 */
void CreateMachineExpression(Expression * Expression, MachineService * MachineService);

/**
 * 
 */
void CreateJoinExpression(Expression * expression, Expression const * children);


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
