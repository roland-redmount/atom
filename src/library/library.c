
#include "library/library.h"
#include "library/list.h"
#include "library/math.h"
#include "library/MachineService.h"
#include "library/reflect.h"
#include "library/string.h"
#include "memory/paging.h"


void RegisterLibraryFunctions(void)
{
	RegisterMachineServiceFunctions();
	RegisterMathFunctions();
	RegisterReflectFunctions();
}


void LoadLibraries(void)
{
	RegisterLibraryFunctions();
	if(GetPersistentState(STATE_KEY_MACHINE_SERVICES)) {
		// Restore persistent state for libraries
		ListRestore();
		MathRestore();
		StringRestore();
		ReflectionRestore();
	}
	else {
		// Setup from scratch
		SetupMachineServices();
		ListSetup();
		MathSetup();
		StringSetup();
		ReflectionSetup();
	}
}


void UnloadLibraries(void)
{
	ReflectionShutdown();
	StringShutdown();
	MathShutdown();
	ListShutdown();
	FreeMachineServices();
}
