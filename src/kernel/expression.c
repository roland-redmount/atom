
#include "kernel/expression.h"


void CreateMachineExpression(Expression * expression, MachineService * machineService)
{
	SetMemory(expression, sizeof(Expression), 0);
	expression->type = EXPRESSION_MACHINE;
	expression->value.machineService = *machineService;
}


void CreateJoinExpression(Expression * expression, Expression const * children)
{
	// TODO
	ASSERT(false)
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


