
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

/**
 * Apart from machine services, which provide the stored and computed relations
 * at the leaves of a service tree, every service is an operator of relational
 * algebra applied to the relations of its child services:
 *
 *   PERMUTE      rename, and restrict on a constant argument     rho, sigma
 *   CONSTRAIN    restrict on an equality between arguments       sigma
 *   JOIN         inner join                                      join
 *   PROJECT      projection                                      pi
 *   UNION        set union                                       union
 *
 * The three operators taking an argument map differ in what their map may do:
 * a permute service may take a child argument from a constant, a constrain
 * service may take several child arguments from one argument, and a join
 * service does neither.
 *
 * Every service provides all of its arguments and yields distinct tuples, so
 * every service yields a valid relation, and an operator can be applied to any
 * service without regard for how that service was composed.
 */
 enum ServiceType {
	/**
	 * PERMUTE is a rename composed with a restriction on constant arguments:
	 * it calls a child service with its arguments reordered, and may bind
	 * constants to child arguments.
	 * Every child argument is either taken from a parent argument or bound to
	 * a constant, so PERMUTE never drops a child argument and hence never
	 * introduces duplicate tuples.
	 */
	SERVICE_PERMUTE = 1,
	/**
	 * JOIN is the inner join of the relations of two child services, on the
	 * arguments they have in common. Each child service has its own argument map
	 * and so keeps its own arity; an argument occurring in both maps is a join
	 * argument, whose value the left child determines and the right child is then
	 * constrained by.
	 */
	SERVICE_JOIN = 2,
	/**
	 * UNION gives the set union of the tuple sets from two child services.
	 * It is assumed that each child service produces tuples in sorted order.
	 *
	 * NOTE: if services are required to be distinct (using preconditions)
	 * then we should never have duplicate tuples in a UNION.
	 */
	SERVICE_UNION = 3,
	/**
	 * PROJECT is the projection onto the first nArguments arguments of its child
	 * service: it drops the remaining ones and removes the duplicate tuples that
	 * dropping may produce. Its tuples are yielded in sorted order.
	 */
	SERVICE_PROJECT = 4,
	/**
	 * Call a machine code function. Machine services are the leaves of a service
	 * tree, providing the relations that the operators above are applied to.
	 */
	SERVICE_MACHINE = 5,
	/**
	 * CONSTRAIN is a restriction on an equality between arguments: it yields those
	 * tuples of its child service in which all child arguments taken from the same
	 * argument of this service are equal. This expresses the equality constraint of
	 * a variable occurring more than once in a query, such as (edge e from x to x)
	 * asking for the self edges of a graph.
	 *
	 * NOTE: this is the only service whose call may consume several child tuples,
	 * as it can only test the constraint once the child service has produced a tuple.
	 */
	SERVICE_CONSTRAIN = 6,
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
			// Each parent argument occurs once; taking several child arguments from
			// one argument is what a constrain service expresses.
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
		// for SERVICE_CONSTRAIN
		struct {
			Service * childService;
			// Index of each child argument into the parent arguments. Unlike the
			// other argument maps this one is not injective: child arguments
			// sharing an index are the ones constrained to be equal.
			index8 * argumentMap;
		} constrain;
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
 *
 * The constants and constantTypes arrays have length nConstants, and may be 0 if
 * there are none. The service acquires a reference to each constant.
 *
 * The child service must provide every parent argument exactly once, so that every
 * parent argument occurs in argumentMap, and none occurs twice. An argument this
 * service does not write would be left at whatever the caller had in the arguments
 * tuple, and so would not be part of a relation; taking several child arguments from
 * one argument is what a constrain service expresses.
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
 * The two child services must together provide every parent argument, so that every
 * parent argument occurs in leftMap or rightMap: an argument neither child writes
 * would be left at whatever the caller had in the arguments tuple. Neither map may
 * contain the same argument twice; taking several child arguments from one argument
 * is what a constrain service expresses.
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
 * Create a CONSTRAIN service with the given number of arguments.
 * The argumentMap array has length equal to childService->nArguments and gives the
 * index of each child argument into the arguments of this service. Child arguments
 * sharing an index are constrained to be equal: only those tuples of the child
 * service in which they are equal are yielded.
 *
 * The child service must provide every argument, as for a permute service.
 */
Service * CreateConstrainService(
	size8 nArguments, index8 const * argumentMap, Service * childService);

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
