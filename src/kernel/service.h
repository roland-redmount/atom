/**
 * A service record is a description of a service as a hierarchy of sub-services,
 * terminating in "machine" services which hold pointer to C functions.
 * 
 * The compiler creates service records from rule evaluation,
 * converting conjunction to JOINs &c.
 * Service records could be used to generate bytecode, serving as an intermediate
 * representation.
 */

#ifndef SERVICE_H
#define SERVICE_H

#include "kernel/tuple.h"
#include "btree/btree.h"


/**
 * TODO: for bytecode to be able to create contexts from services
 * (instrution CTX), this structure must be represented as an Atom.
 * Currently we use AT_BYTECODE but this must be generalized
 * to cover native services.
 */

enum ServiceType {
	SERVICE_NONE = 0,
	// SERVICE_BYTECODE,
	SERVICE_MACHINE,
	SERVICE_JOIN,
	SERVICE_UNION,
};

typedef struct s_MachineService MachineService;
typedef struct s_ServiceRecord ServiceRecord;
// typedef struct s_ServiceContext ServiceContext;

/**
 * For "machine" services like B-tree, arrays, basic arithmetic,
 * we need a record storing pointers to the C code.
  */
struct s_MachineService {
	/**
	 * For data storage services like B-trees, the setup/call/free functions
	 * are always the same, but parameterized by the specific B-tree used -- a kind of
 	 * "hyperparameter".
	 * 
	 * TODO: there is really two levels of information: (1) service provider information,
	 * e.g. B-Tree that holds the function pointers, and (2) service information, which
	 * holds the specific BTree * pointer. (And (3) execution context, which holds the arguments)
	 * We should separate out the service provider part so we don't repeatedly store
	 * the same function pointers over and over.
	 */
	data64 serviceParameter;

	//-------------------- execution context functions -----------------
	/**
	 * Initialize service-specific context information, such as an iterator structure.
	 * This method must return a pointer to its context (or 0 if none).
	 * This context pointer will then be supplied to call() and finalizeContext().
	 */
	void * (*setupContext)(MachineService * service, Tuple const * arguments);

	/**
	 * Call (resume) an executing service, return true if a tuple was produced,
	 * false if evaluation terminated. Writes to the given tuple.
	 */
	bool (*call)(void * context, Tuple * result);

	/**
	 * Any code that needs to run to finalize the service after termination
	 */
	void (*finalizeContext)(void * context);

	//------------ functions for tuple-storing services -----------------
	
	/**
	 * Function for adding a tuple to the service, or 0 if not supported.
	 */
	void (*addTuple)(MachineService * service, Tuple const * arguments);

	/**
	 * Function for removing tuples to the service, or 0 if not supported.
	 * The arguments tuple may contain variables.
	 */
	void (*removeTuples)(MachineService * service, Tuple const * arguments);

	bool (*isEmpty)(MachineService const * service);

	void (*teardown)(MachineService * service);

};

/**
 * A "composite" service is a JOIN or UNION expression across two services.
 * TODO: this must be stored persistently, should not use Allocate()
 * for long-term storage.
 * When executed, the execution context should cache pointers to the 
 * sub-service contexts.
 */
typedef struct s_CompositeService {
	// Mapping between arguments of this service
	// and parameters of each sub-service.
	// TODO: for now we use a fixed maximum number of arguments
	index8 leftArgumentMap[8];
	index8 rightArgumentMap[8];
	Atom leftService;
	Atom rightService;
} CompositeService;

/**
 * This should perhaps be internal to ServiceRegistry?
 */
struct s_ServiceRecord {
	// service hash value
	Atom service;
	// we store the form and parameters lists of the signature separately
	// to allow iterating across all services matching a given form
	Atom form;
	Atom parameters;
	enum ServiceType type;
	union {
		MachineService machineService;
		CompositeService compositeService;
		Atom bytecode;
	} provider;
};


/**
 * A context holding the runtime information for an executing service.
 * Analogous to VM contexts.
 */
// struct s_ServiceContext {
// 	ServiceRecord const * service;
// 	Tuple * arguments;
// 	// I hope this can be handled elsewhere, by dispatch
// 	// index8 * permutation;
// };

// Tuple * ServiceContextGetArguments(ServiceContext * context);



// -------- is any of the below necessary? Implemented by each machine service separately ---------------

/**
 * Create an evaluation context for an expression with a given argument tuple.
 * This is a form of co-routine call, similar to other iterators.
 * 
 * NOTE: this might even make bytecode evaluation unnecessary?
 * 
 * The argumentMap vector maps arguments from caller to the expression,
 * avoiding the need to copy tuples between services in the expression hierarchy.
 * This vector would typically be defined by unification.
 */

// void ExpressionIterate(Expression const * expression, Tuple * arguments, index8 * argumentMap, EvaluationContext * context);

/**
 * Resume evaluating the expression, return true if a tuple was produced,
 * false if evaluation terminated.
 * If true, values are written to the arguments tuple supplied to ExpressionIterate()
 */
// bool ExpressionNext(EvaluationContext * context);

/**
 * Check if the evaluation has yielded a tuple.
 */
// void ExpressionHasTuple(EvaluationContext * context);

/**
 * Terminate evaluation
 */
// void ExpressionEnd(EvaluationContext * context);


#endif		// SERVICE_H
