#include "kernel/service.h"
#include "kernel/ifact.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "library/math.h"
#include "memory/allocator.h"
#include "parser/PredicateBuilder.h"


/**
 * The service (= z>INT + x<INT + y<INT)
 */
static void add1(ServiceContext * context)
{
	// TODO: how to index into the tuple reliably?
	// Currently we are hardcoding canonical positions of the x, y, z roles
	int64 x = TupleGetAtom(context->arguments, 1)._int;
	int64 y = TupleGetAtom(context->arguments, 2)._int;
	// TODO: typed services really should not have to write argument types ...
	TupleSetElement(context->arguments, 0, CreateTypedAtom(AT_INT, (Atom) {._int = x + y}));
}

/**
 * The service (= z<INT + x<INT + y>INT)
 * This implements subtraction by solving the equation
 * z = x + y  <->  y = z - x
 */
static void add2(ServiceContext * context)
{
	int64 x = TupleGetAtom(context->arguments, 1)._int;
	int64 z = TupleGetAtom(context->arguments, 0)._int;
	TupleSetElement(context->arguments, 2, CreateTypedAtom(AT_INT, (Atom) {._int = z - x}));
}

/**
 * Lookup table for service functions.
 * We need this since we cannot store a function pointer directly in void * providerData
 */

#define ADD1_INDEX		0
#define ADD2_INDEX		1
// etc ...

#define N_SERVICES 		2

typedef void (*MathFunction)(ServiceContext * context);

MathFunction functionTable[N_SERVICES] = {
	&add1,
	&add2,
};

// List of corresponding service record (keys)
// These are needed by MathTeardown()
ServiceRecord mathServices[N_SERVICES];


/**
 * TODO: Argument index table, initialized during setup 
 */
#define MAX_N_ARGUMENTS 3
// index8 argumentIndexTable[N_SERVICES][MAX_N_ARGUMENTS];

typedef struct s_MathContext {
	MathFunction function;
	bool hasBeenCalled;
} MathContext;


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


static void setupAdd1(void)
{
	// NOTE: this must be in canonical order and arguments numbered accordingly
	Atom formula = CStringToPredicate("= @1>INT + @2<INT + @3<INT");
	// PrintFormula(formula);
	// PrintChar('\n');
	// PredicateRoleIndex(form, roles[j]);

	ServiceRecord record = {
		.form = FormulaGetForm(formula),
		.parameters = FormulaGetActors(formula),
		.service = CreateMachineService(3, &mathServiceProvider, (void *) ADD1_INDEX)
	};
	RegistryAddService(&record);
	ReleaseService(record.service);
	mathServices[ADD1_INDEX] = record;
	IFactRelease(formula);
}


static void setupAdd2(void)
{
	Atom formula = CStringToPredicate("= @1<INT + @2<INT + @3>INT");
	ServiceRecord record = {
		.form = FormulaGetForm(formula),
		.parameters = FormulaGetActors(formula),
		.service = CreateMachineService(3, &mathServiceProvider, (void *) ADD2_INDEX)
	};
	RegistryAddService(&record);
	ReleaseService(record.service);
	mathServices[ADD2_INDEX] = record;
	IFactRelease(formula);
}


void MathSetup(void)
{
	setupAdd1();
	setupAdd2();
}


void MathTeardown(void)
{
	for(index32 i = 0; i < N_SERVICES; i++)
		RegistryRemoveService(&mathServices[i]);
}
