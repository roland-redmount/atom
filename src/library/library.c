
#include "library/library.h"
#include "library/list.h"
#include "library/math.h"
#include "library/MachineService.h"
#include "library/reflect.h"
#include "library/string.h"


void RegisterLibraryFunctions(void)
{
	RegisterMachineServiceFunctions();
	RegisterMathFunctions();
	RegisterReflectFunctions();
}


void LoadLibraries(void)
{
	RegisterLibraryFunctions();
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
