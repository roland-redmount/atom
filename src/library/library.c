
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
	// CLAUDE: in a restored paging area, the libraries are already loaded
	if(GetPersistentState(STATE_KEY_MACHINE_SERVICES)) {
		ListRestore();
		MathRestore();
		StringRestore();
		ReflectionRestore();
		return;
	}
	SetupMachineServices();
	ListSetup();
	MathSetup();
	StringSetup();
	ReflectionSetup();
}


void UnloadLibraries(void)
{
	ReflectionShutdown();
	StringShutdown();
	MathShutdown();
	ListShutdown();
	FreeMachineServices();
}
