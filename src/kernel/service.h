/**
 * A service record is a description of a service as a hierarchy of sub-services,
 * terminating in "machine" services which hold pointer to C functions.
 * 
 * The compiler creates service records from rule evaluation,
 * converting conjunction to JOINs &c.
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
 * Services are either expressions or machine services.
 */

enum ServiceType {
	SERVICE_NONE = 0,
	SERVICE_MACHINE,
	SERVICE_EXPRESSION,	// compiled rule, intermediate representation
};

typedef struct s_MachineService MachineService;
typedef struct s_ServiceRecord ServiceRecord;


#endif		// SERVICE_H
