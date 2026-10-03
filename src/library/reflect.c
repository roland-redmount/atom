#include "kernel/MixedTypeRelation.h"
#include "library/MachineService.h"
#include "library/reflect.h"

/**
 * (formula #1<FORMULA arity #2>INT)
 * 
 * The arity of a reflected formula
 */
static bool formulaArityCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[1]._int = FormulaArity(arguments[0]);
	return true;
}

/**
 * (query #1<FORMULA relation #2>RELATION)
 * 
 * TODO: return the relation defined by the query formula.
 * Does not actually compute a query; merely wraps the formula in an
 * AT_RELATION atom.
 */
static bool queryRelationCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[1] = arguments[0];
	return true;
}


/**
 * (relation #1<RELATION size #2>INT)
 * 
 * Perform the query given by the formula held by the
 * RELATION atom, count the rows and return the result.
 */
static bool relationSizeCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	// A RELATON atom stores the same atom (hash value) as a FORMULA
	FormulaView query = FormulaGetView(arguments[0]);
	MixedTypeRelation * relation = CreateConcatRelation(query);
	int64 nTuples = 0;
	while(MixedTypeRelationNext(relation))
		nTuples++;
	FreeMixedTypeRelation(relation);
	arguments[1]._int = nTuples;
	return true;
}


static uint32 moduleID;

void ReflectionSetup(void)
{
	moduleID = RequestModuleID();

	RegisterMachineService(moduleID, "formula #1<FORMULA arity #2>INT", formulaArityCall);

	RegisterMachineService(moduleID, "query #1<FORMULA relation #2>RELATION", queryRelationCall);

	RegisterMachineService(moduleID, "relation #1<RELATION size #2>INT", relationSizeCall);
}


void ReflectionShutdown(void)
{
	FreeModuleRelations(moduleID);
}
