
#include "library/library.h"
#include "library/list.h"
#include "library/math.h"
#include "library/MachineService.h"
#include "library/pair.h"
#include "library/string.h"


void LoadLibraries(void)
{
	ListSetup();
	MathSetup();
	PairSetup();
	StringSetup();
}


void UnloadLibraries(void)
{
	/* CLAUDE: the math services are machine services, removed by FreeMachineServices().
	   The remaining libraries are shut down in reverse load order, since the string
	   relations are built on the list relations; see StringSetup(). */
	FreeMachineServices();
	StringShutdown();
	PairShutdown();
	ListShutdown();
}
