
 #ifndef SERVICE_H
 #define SERVICE_H

 #include "lang/Atom.h"


typedef struct s_Service Service;
typedef struct s_ServiceContext ServiceContext;

/**
 * A machine service provider is an implementation of a particular type
 * of machine services, such as B-Tree relations or arithmetic functions.
 * One MachineServiceProvider can provide multiple MachineService for
 * various relations.
 */
 
typedef bool (*MachineServiceCall)(ServiceContext * context);

typedef struct s_MachineServiceProvider {
	/**
	 * Initialize service-specific context information, such as an iterator structure.
	 * context will point to an allocate block of at least 
	 * This method must return a pointer to its context (or 0 if none).
	 * This context pointer will then be supplied to call() and finalizeContext().
	 */
	void (*setupContext)(ServiceContext * context);

	/**
	 * Call (resume) an executing service, return true if a tuple was produced,
	 * false if evaluation terminated. The call() function must write to the 
	 * arguments tuple, so the context must keep a pointer to this tuple.
	 * If the various services provided need different entry points, this function
	 * is responsible for calling the relevant one.
	 */
	MachineServiceCall call;

	/**
	 * Finalize a service context after termination.
	 * This pointer may be 0 if no finalization is required.
	 */
	void (*finalizeContext)(ServiceContext * context);

	/**
	 * Finalize the machine service (deallocate data structures, &c).
	 * This pointer may be 0 if no finalization is required.
	 */
	void (*finalizeService)(Service * service);

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
 * 
 * TODO: services should specify how their tuples are ordered when enumerating.
 * Currently, B-Tree services order tuples lexiographically, and machine services
 * do not enforce any particular order. It might be a good idea to provide an
 * array of column numbers specifying the ordering, so that e.g. a relation
 * with form (a b c) might specify ordering = {2, 1, 3} to order lexiographically
 * w.r.t columns (b a c). Knowing the tuple order helps optimize PROJECT: a child
 * ordered on the columns PROJECT keeps lets it drop duplicates by comparing each
 * tuple to its predecessor, instead of materializing the whole child relation.
 *
 */
 enum ServiceType {
	/**
	 * PERMUTE calls a child service with its arguments reordered,
	 * and may optionally bind constants to child arguments.
	 * Every child argument is either taken from a parent argument or bound to
	 * a constant, so PERMUTE never drops a child argument and hence never
	 * introduces duplicate tuples.
	 */
	SERVICE_PERMUTE = 1,
	/**
	 * inner join between two "child" services
	 */
	SERVICE_JOIN = 2,
	/**
	 * UNION gives a union of the tuple sets from two child services.
	 * It is assumed that each child service produces tuples in sorted order.
	 * 
	 * NOTE: if services are required to be distinct (using preconditions)
	 * then we should never have duplicate tuples in a UNION.
	 */
	SERVICE_UNION = 3,
	/**
	 * PROJECT drops all but the first nArguments arguments of its child service
	 * and removes the duplicate tuples that dropping may produce. Its tuples are
	 * yielded in sorted order.
	 */
	SERVICE_PROJECT = 4,
	/**
	 * Call a machine code function
	 */
	SERVICE_MACHINE = 5,
};

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
			// Stored constants and their atom types, addressed by the argument map
			Atom * constants;
			byte * constantTypes;
			size8 nConstants;
			// Source of each child argument: an index below nArguments is a parent
			// argument, an index of nArguments or above is constants[index - nArguments].
			// NOTE: the parent:child mapping is 1:n, a parent argument
			// may be repeated at multiple positions in the child arguments tuple
			index8 * argumentMap;
		} permute;
		// for SERVICE_JOIN
		struct {
			Service * left;
			Service * right;
			// Indices of each child argument into the parent arguments.
			// Unlike a permute service, every child argument maps to a parent
			// argument: a join service neither binds constants nor drops arguments.
			index8 * leftMap;
			index8 * rightMap;
		} join;
		// for SERVICE_UNION
		struct {
			Service * first;
			Service * second;
		} _union;
		// for SERVICE_PROJECT
		struct {
			Service * childService;
		} project;
		// for SERVICE_MACHINE
		struct {
			MachineServiceProvider * provider;
			void * providerData;
		} machine;
	} impl;
};

/**
 * Create a permute service with the specified number of arguments.
 * The argumentMap array has length equal to childService->nArguments and gives the
 * source of each child argument: an index below nArguments is the index of a parent
 * argument, an index of nArguments or above refers to constants[index - nArguments].
 * One parent argument may be the source of multiple child arguments, in which case
 * its index is repeated.
 *
 * The constants and constantTypes arrays have length nConstants, and may be 0 if
 * there are none. The service acquires a reference to each constant.
 *
 * NOTE: If some parent arguments are missing from argumentMap, those arguments will not be
 * updated by this service, so its tuples leave those arguments undefined. Such a permute service is
 * only valid as a child of a join service, whose children together cover every parent argument.
 */
Service * CreatePermuteService(
	size8 nArguments, Atom const * constants, byte const * constantTypes, size8 nConstants,
	index8 const * argumentMap, Service * childService);

/**
 * Create a machine code service
 */
Service * CreateMachineService(size8 nArguments, MachineServiceProvider * provider, void * providerData);

/**
 * Setup a JOIN service with the specified number of arguments, from two existing
 * child services. The left child service will execute first, and may determine
 * input arguments for the right child service.
 *
 * The leftMap and rightMap arrays have length equal to the number of arguments of
 * the respective child service, and give for each child argument its index
 * into the parent arguments tuple. An argument occurring in both maps is a join
 * argument: the left child service determines its value, which then constrains
 * the right child service.
 *
 * NOTE: the two maps should together cover every parent argument, or the tuples
 * of this service leave some arguments undefined.
 */
Service * CreateJoinService(
	size8 nArguments,
	Service * leftChild, index8 const * leftMap,
	Service * rightChild, index8 const * rightMap);

/**
 * Setup a UNION service, returning the union of two relations.
 */
Service * CreateUnionService(Service * first, Service * second);

/**
 * Create a PROJECT service with the given number of arguments, which must be
 * less than the number of arguments of the child service. The project service
 * keeps the first nArguments arguments of the child service, drops the remaining
 * ones, and removes any duplicate tuples resulting from this.
 */
Service * CreateProjectService(Service * childService, size8 nArguments);

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
struct s_ServiceContext {
	Service const * service;
	Atom * arguments;
	byte data[];
};

 /**
  * Create and return an execution context for evaluating a service
  * with the given arguments tuple. Each ServiceCall() to this context
  * will write its result into the arguments tuple.
  */
ServiceContext * ServiceCreateContext(Service const * service, Atom arguments[]);

/**
 * Execute a service with a given context. Returns true if a tuple was produced,
 * or false if no more tuples are available. Once this function has returned false,
 * it must not be called again.
 */
bool ServiceCall(ServiceContext * context);

/**
 * Finalize a service context, releasing any allocated resources.
 */
void ServiceFreeContext(ServiceContext * context);

/**
 * Initialize a service context, perform one call, and terminate.
 * The service must produce at most one tuple for the given arguments.
 * Return true if a tuple was produced.
 */
bool ServiceCallOnce(Service const * service, Atom arguments[]);

/**
 * Print service information.
 */
void PrintService(Service const * service);


#endif	// SERVICE_H
