
 #ifndef SERVICE_H
 #define SERVICE_H

 #include "kernel/machineservice.h"

 
/**
 * A service is a procedure for evaluate queries of a particular form:
 * either a machine procedure or a compiled intermediate representation.
 * A compiled service consists of child services, forming a tree.
 * The "leaves" of this tree are always machine services.
 * A service is evaluated stepwise, at each call yielding one tuple,
 * similar to a co-routine.
 */
 enum ServiceType {
	SERVICE_JOIN,
	SERVICE_UNION,
	SERVICE_PROJECT,
	SERVICE_MACHINE,
};

typedef struct s_Service Service;

struct s_Service {
	enum ServiceType type;
	struct {
		// Length of arguments tuple for this service
		size32 nArguments:8;
		// Context size, in addition to sizeof(Context)
		size32 contextSize:24;
	} dimensions;
	// Indices into argument tuple
	// Fixed size for now; need to figure out allocation
	index8 argumentMap[8];
	union {
		// for SERVICE_JOIN
		struct {
			Service const * left;
			Service const * right; 
		} children;
		// for SERVICE_MACHINE (leaves)
		MachineService machineService;
	} value;
};

/**
 * Create a machine service
 */
void SetupMachineService(
	Service * service, size8 nArguments, index8 const * argumentMap, MachineService const * machineService);

/**
 * Create a join service from two existing services
 */
void SetupJoinService(
	Service * service, size8 nArguments, index8 const * argumentMap,
	Service const * leftChild, Service const * rightChild);


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

/**
 * Read and write context arguments
 */
TypedAtom ServiceContextReadArgument(ServiceContext * context, index8 index);

void ServiceContextWriteArgument(ServiceContext * context, index8 index, TypedAtom argument);


void PrintService(Service const * service);


#endif	// SERVICE_H
