
 #ifndef SERVICE_H
 #define SERVICE_H

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
	void (*setupContext)(struct s_ServiceContext * context);

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

	size32 contextSize;

} MachineServiceProvider;


/**
 * A service is either a machine procedure or an operation on relations,
 * created by the compiler from specific queries. Compiled services may
 * depend on any other service found in the service registry, so services
 * must be reference counted.
 * 
 * A service is evaluated stepwise, at each call yielding one tuple,
 * similar to a co-routine.
 */
 enum ServiceType {
	SERVICE_PERMUTE = 1,	// permute the arguments of another service
							// NOTE: this is a special case of PROJECT
	SERVICE_JOIN = 2,		// inner join between two "child" services
	SERVICE_UNION = 3,		// union of tuple sets of child services
	SERVICE_PROJECT = 4,	// drop arguments and remove duplicates
	SERVICE_MACHINE = 5,	// call a machine code function
};

typedef struct s_Service Service;

struct s_Service {
	enum ServiceType type;
	// Number of arguments for this service
	size8 nArguments;
	// Context size, in addition to sizeof(Context)
	size32 contextSize;
	size32 referenceCount;
	union {
		// for SERVICE_PERMUTE
		struct {
			Service * childService;
			// Stored constants
			Tuple * constants;
			// 1-based indices of each child argument into the parent arguments,
			// or 0 if the child argument is a constant.
			// NOTE: the parent:child mapping is 1:n, a parent argument
			// may be repeated at multiple positions in the child arguments tuple
			index8 * argumentMap;
		} permute;
		// for SERVICE_JOIN
		struct {
			Service * left;
			Service * right; 
		} join;
		// for SERVICE_MACHINE
		struct {
			MachineServiceProvider * provider;
			void * providerData;
		} machine;
	} impl;
};

/**
 * Create a permute service. The argumentMap array has length equal to childService->nArguments
 * and specifies for each child argument either a 1-based index into the parent arguments tuple,
 * or 0 for a constant, in the order of the constants tuple. One parent argument may map to
 * multiple child arguments, in which case parent indices are repeated.
 * If some parent argument positions are missing from argumentMap, those arguments will not be
 * updated by this service. This typically occurs when the permute service is a child of a join service.
 */
Service * CreatePermuteService(
	size8 nArguments, Tuple const * constants, index8 const * argumentMap, Service * childService);

/**
 * Create a machine service
 */
Service * CreateMachineService(size8 nArguments, MachineServiceProvider * provider, void * providerData);

/**
 * Setup a join service from two existing child services. The left child service
 * will execute first, and may determine input arguments for right child service.
 * The two child services must take the same arguments, in the same order.
 */
Service * CreateJoinService(Service * leftChild, Service * rightChild);

/**
 * Acquire a reference to a service.
 */
void AcquireService(Service * service);


/**
 * Remove one reference to the given service, deallocate if references reach zero.
 */
void ReleaseService(Service * service);


/**
 * Executing a service consists of setting up a service execution context,
 * performing one or more calls against that context, and finalizing the context.
 * Sub-services will have their own execution contexts, which are initialized
 * as necessary.
 */
typedef struct s_ServiceContext {
	Service const * service;
	Tuple * arguments;
	byte data[];
} ServiceContext;


 /**
  * Create and return an execution context for evaluating a service
  * with the given arguments tuple. Each ServiceCall() to this context
  * will write its result into the arguments tuple.
  */
ServiceContext * ServiceCreateContext(Service const * service, Tuple * arguments);

/**
 * Execute a service with a given context. This is the interpreter.
 */
bool ServiceCall(ServiceContext * context);

/**
 * Finalize a service context, releasing any allocated resources.
 */
void ServiceFreeContext(ServiceContext * context);


void PrintService(Service const * service);


#endif	// SERVICE_H
