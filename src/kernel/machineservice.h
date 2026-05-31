/**
 * A MachineServiceProvider is an machine level implementation of a service,
 * such as RelationBTree. A service provider can provide multiple services
 * with different forms, and so have multiple entries in the service registry.
 */

#ifndef MACHINE_SERVICE_H
#define MACHINE_SERVICE_H

#include "kernel/tuple.h"


/**
 * A machine service provider is an implementation of a particular type
 * of machine services, such as B-Tree relations or arithmetic functions.
 * One MachineServiceProvider can host multiple MachineService for
 * specific relations.
 */
typedef struct s_MachineServiceProvider {
	/**
	 * Initialize service-specific context information, such as an iterator structure.
	 * This method must return a pointer to its context (or 0 if none).
	 * This context pointer will then be supplied to call() and finalizeContext().
	 */
	void * (*createContext)(void * providerData, Tuple const * arguments);

	/**
	 * Call (resume) an executing service, return true if a tuple was produced,
	 * false if evaluation terminated. Writes to the given tuple.
	 */
	bool (*call)(void * context, Tuple * result);

	/**
	 * Any code that needs to run to finalize the service after termination
	 */
	void (*freeContext)(void * context);

} MachineServiceProvider;


/**
 * A specific machine service, implemented by a provider, such as a particular B-tree,
 * or a particular arithmetic function.
 */
typedef struct s_MachineService {
	MachineServiceProvider * provider;
	void * providerData;
} MachineService;


/**
 * Interface functions for convenien
 */
void * MachineServiceCreateContext(MachineService const * service, Tuple const * arguments);

bool MachineServiceCall(MachineService const * service, void * context, Tuple * result);

void MachineServiceFreeContext(MachineService const * service, void * context);


#endif	// SERVICE_PROVIDER_H
