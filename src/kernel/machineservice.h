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
 * One MachineServiceProvider can provide multiple MachineService for
 * various relations.
 */
 
struct s_ServiceContext;

typedef bool (*MachineServiceCall)(struct s_ServiceContext * context);

typedef struct s_MachineServiceProvider {
	/**
	 * Initialize service-specific context information, such as an iterator structure.
	 * context will point to an allocate block of at least 
	 * This method must return a pointer to its context (or 0 if none).
	 * This context pointer will then be supplied to call() and finalizeContext().
	 */
	void (*setupContext)(struct s_ServiceContext * context, void * providerData);

	/**
	 * Call (resume) an executing service, return true if a tuple was produced,
	 * false if evaluation terminated. The call() function must write to the 
	 * arguments tuple, so the context must keep a pointer to this tuple.
	 * If the various services provided need different entry points, this function
	 * is responsible for calling the relevant one.
	 */
	MachineServiceCall call;

	/**
	 * Any code that needs to run to finalize the service after termination
	 */
	void (*finalizeContext)(struct s_ServiceContext * context);

} MachineServiceProvider;


/**
 * A specific machine service, implemented by a provider, such as a particular B-tree,
 * or a particular arithmetic function.
 */
typedef struct s_MachineService {
	MachineServiceProvider * provider;
	size32 contextSize;
	void * providerData;
} MachineService;


#endif	// SERVICE_PROVIDER_H
