/**
 * A service record is a description of a service as a hierarchy of sub-services,
 * terminating in "machine" services which hold pointer to C functions.
 * 
 * The compiler creates service records from rule evaluation,
 * converting conjunction to JOINs &c.
 * Service records could be used to generate bytecode, serving as an intermediate
 * representation.
 * 
 * NOTE: a key architecture decision is whether intermediate representation (IR) of
 * rules, such as JOIN(A(x,y), PROJECT(B(x,y,z), {x,y})) should be stored as services or
 * in a separate form. To use the IR for execution, it should be optimized and
 * probably not stored natively as atoms. On the other hand it must be persistent.
 * The top expression for an IR is a compiled service, and must be entered into
 * the service registry, but sub-expression such as PROJECT(B(x,y,z), {x,y}) in the above
 * should probably not be entered as services in their own right. So an IR can be
 * used as a service, but not all IRs are services. The "leaf" expression in an IR
 * are machine services, so the IR must "contiain" services as well.
 * IR must be separate from the rule dictionary since one rule may yield multiple IR
 * when compiled with different arguments.
 */

#ifndef SERVICE_H
#define SERVICE_H

#include "kernel/expression.h"
#include "kernel/tuple.h"
#include "btree/btree.h"


/**
 * TODO: for bytecode to be able to create contexts from services
 * (instrution CTX), this structure must be represented as an Atom.
 * Currently we use AT_BYTECODE but this must be generalized
 * to cover native services.
 * 
 * NOTE: SERVICE_JOIN or SERVICE_UNION is also implied by the service form
 * being a clause form or 
 */

enum ServiceType {
	SERVICE_NONE = 0,
	// SERVICE_BYTECODE,
	SERVICE_MACHINE,
	SERVICE_EXPRESSION,	// compiled rule, intermediate representation
};

typedef struct s_MachineService MachineService;
typedef struct s_ServiceRecord ServiceRecord;
// typedef struct s_ServiceContext ServiceContext;



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
