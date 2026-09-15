
#include "lang/formula.h"
#include "library/ifactservice.h"
#include "library/MachineService.h"

/**
 * The operator (id x>ID ifact y<FORMULA)
 * If the formula does not represent a valid ifact, this returns false
 */
static bool idIFactCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	// FormulaView formulaView = FormulaGetView(arguments[1]);
	Atom id = {0};

	/* TODO: this does not work, since CreateIFact() asserts facts,
	   which a service cannot do. We need to factorize ifact creation into two parts:
	   (1) a service that computes the hash and returns the ID atom
	   (2) a means of either asserting or returning the ifacts.
	   Only part 1 belongs in this service.
	 */ 
	// Atom id = CreateIFact(formulaView);

	if(!id.hash)
		return false;		// not a valid ifact
	else {
		arguments[0] = id;
		return true;
	}
}


static uint32 moduleID;


void IFactServiceSetup(void)
{
	moduleID = RequestModuleID();

	RegisterMachineService(moduleID, "id @1>ID ifact @2<FORMULA", idIFactCall);
}


void IFactServiceShutdown(void)
{
	FreeModuleRelations(moduleID);
}