#include "library/MachineService.h"
#include "library/reflect.h"

/**
 * (formula #1<FORMULA arity #2>INT)
 * 
 * The arity of a reflected formula
 */
static bool arityCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[1]._int = FormulaArity(arguments[0]);
	return true;
}


static uint32 moduleID;

void ReflectionSetup(void)
{
	moduleID = RequestModuleID();

	RegisterMachineService(moduleID, "formula #1<FORMULA arity #2>INT", arityCall);
}


void ReflectionShutdown(void)
{
	FreeModuleRelations(moduleID);
}
