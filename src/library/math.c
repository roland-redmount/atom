#include "kernel/Parameter.h"
#include "kernel/service.h"
#include "kernel/ifact.h"
#include "kernel/ServiceRegistry.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "library/math.h"
#include "memory/allocator.h"
#include "parser/PredicateBuilder.h"


#define ADD1_INDEX		0
#define ADD2_INDEX		1
// etc ...
#define N_SERVICES 		2


#define ADD_RELATION	0
// ...
#define N_RELATIONS		1

#define MAX_RELATION_ARITY	3


// List of created relation tables
// These are needed by MathTeardown()
RelationTable const * mathRelations[N_RELATIONS];

// Precomputed argument indexes, in "reference" order
// TODO: This is similar to kernel.corePredicateRoleIndex,
// we should have a more general mechanism for handling "user order"
static index8 relationArgumentIndex[N_RELATIONS][MAX_RELATION_ARITY];

/**
 * The service (= z>INT + x<INT + y<INT)
 */
static void add1(ServiceContext * context)
{
	int64 x = context->arguments[1]._int;
	int64 y = context->arguments[2]._int;
	context->arguments[1]._int = x + y;
}

static void setupAdd1(void)
{
	// Argument indices w.r.t. "user order" (+ + =)
	index8 * argumentIndex = relationArgumentIndex[ADD_RELATION];
	byte parameterIO[3];
	parameterIO[argumentIndex[0]] = PARAMETER_IN;
	parameterIO[argumentIndex[1]] = PARAMETER_IN;
	parameterIO[argumentIndex[2]] = PARAMETER_OUT;

	Service * service = CreateMachineService(3, &mathServiceProvider, (void *) ADD1_INDEX);
	RelationAddService(mathRelations[ADD_RELATION], parameterIO, service);
	ReleaseService(service);
}

/**
 * The service (= z<INT + x<INT + y>INT)
 * This implements subtraction by solving the equation
 * z = x + y  <->  y = z - x
 */
static void add2(ServiceContext * context)
{
	int64 x = context->arguments[1]._int;
	int64 z = context->arguments[0]._int;
	context->arguments[2]._int = z - x;
}


static void setupAdd2(void)
{
	index8 * argumentIndex = relationArgumentIndex[ADD_RELATION];
	byte parameterIO[3];
	parameterIO[argumentIndex[0]] = PARAMETER_IN;
	parameterIO[argumentIndex[1]] = PARAMETER_IN;
	parameterIO[argumentIndex[2]] = PARAMETER_OUT;

	Service * service = CreateMachineService(3, &mathServiceProvider, (void *) ADD2_INDEX);
	RelationAddService(mathRelations[ADD_RELATION], parameterIO, service);
	ReleaseService(service);	
	// mathServices[ADD2_INDEX] = record;
}

/**
 * Lookup table for service functions.
 * We need this since we cannot store a function pointer directly in void * providerData
 * (due to text/data segment issues)
 */
typedef void (*MathFunction)(ServiceContext * context);

typedef struct s_MathContext {
	MathFunction function;
	bool hasBeenCalled;
} MathContext;

MathFunction functionTable[N_SERVICES] = {
	&add1,
	&add2,
};


/**
 * Stubs for the service provider
 */
static void serviceSetupContext(ServiceContext * context)
{
	MathContext * mathContext = (MathContext *) &context->data;
	index32 functionIndex = (data64) context->service->impl.machine.providerData;
	mathContext->function = functionTable[functionIndex];
	mathContext->hasBeenCalled = false;
}


static bool serviceCall(ServiceContext * context)
{
	MathContext * mathContext = (MathContext *) &context->data;
	if(mathContext->hasBeenCalled)
		return false;
	mathContext->function(context);
	mathContext->hasBeenCalled = true;
	return true;
}


static void serviceFinalizeContext(ServiceContext * context)
{
	// Nothing to do
}


MachineServiceProvider mathServiceProvider = {
	.setupContext = &serviceSetupContext,
	.call = &serviceCall,
	.finalizeContext = &serviceFinalizeContext,
};


void MathSetup(void)
{
	// create forms and setup argument indices
	Atom plus = CreateNameFromCString("+");
	Atom equals = CreateNameFromCString("=");
	Atom addForm = CreatePredicateForm((Atom[]) {plus, plus, equals}, 3);
	NameRelease(plus);
	NameRelease(equals);
	index8 * argumentIndex = relationArgumentIndex[ADD_RELATION];
	// Map roles in our "user order" (+ + =) to canonical order
	argumentIndex[0] = PredicateRoleIndex(addForm, plus);
	argumentIndex[1] = argumentIndex[0] + 1;
	argumentIndex[2] = PredicateRoleIndex(addForm, equals);

	// create relation tables
	byte atomTypes[3];
	atomTypes[argumentIndex[0]] = AT_INT;
	atomTypes[argumentIndex[1]] = AT_INT;
	atomTypes[argumentIndex[2]] = AT_INT;
	// NOTE: no particular column index order here
	mathRelations[ADD_RELATION] = CreateRelationTable(0, addForm, 3, atomTypes, 0);

	// setup services
	setupAdd1();
	setupAdd2();
}


void MathTeardown(void)
{
	for(index32 i = 0; i < N_RELATIONS; i++)
		RegistryRemoveRelationTable(mathRelations[i]);
}
