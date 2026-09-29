
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
	StringShutdown();
	PairShutdown();
	MathShutdown();
	ListShutdown();
}
