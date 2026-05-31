
#include "kernel/machineservice.h"


void * MachineServiceCreateContext(MachineService const * service, Tuple const * arguments)
{
	return service->provider->createContext(service->providerData, arguments);
}


bool MachineServiceCall(MachineService const * service, void * context, Tuple * result)
{
	return service->provider->call(context, result);
}


void MachineServiceFreeContext(MachineService const * service, void * context)
{
	service->provider->freeContext(context);
}

