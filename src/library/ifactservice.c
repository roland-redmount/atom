
#include "lang/formula.h"
#include "library/ifactservice.h"
#include "library/MachineService.h"
#include "ui/assert.h"		// for CreateIFact(); should perhaps be moved somewhere else

/**
 * The operator (id x>ID ifact y<FORMULA)
 * If the formula does not represent a valid ifact, this returns false
 */
static bool idIFactCall(MachineOperatorContext * context)
{
	FormulaView formulaView = FormulaGetView(context->arguments[1]);
	Atom id = CreateIFact(formulaView);
	if(!id.hash)
		return false;		// not a valid ifact
	else {
		context->arguments[0] = id;
		return true;
	}
}


static uint32 providerID;


void IFactServiceSetup(void)
{
	providerID = RequestProviderID();

	RegisterMachineService(
		providerID, "id @1>ID ifact @2<FORMULA",
		(MachineOperatorSpec) {.call = idIFactCall}, 0, 0);
		
}


void IFactServiceShutdown(void)
{
	FreeMachineServices(providerID);
}