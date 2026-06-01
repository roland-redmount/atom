
#include "kernel/ifact.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "library/math.h"
#include "memory/allocator.h"
#include "parser/PredicateBuilder.h"


/**
 * The service (= z$INT + x@INT + y@INT)
 */
static void add1(void * context, Tuple * result)
{
	// TODO: how to index into the tuple reliably?
	// We would like to compute the index once and for all.
	int64 x = (int64) TupleGetAtom(result, 1);
	int64 y = (int64) TupleGetAtom(result, 2);
	TupleSetAtom(result, 0, x + y);
}

/**
 * The service (= z@INT + x@INT + y$INT)
 * This implements subtraction by solving the equation
 * z = x + y  <->  y = z - x
 */
static void add2(void * context, Tuple * result)
{
	int64 x = (int64) TupleGetAtom(result, 1);
	int64 z = (int64) TupleGetAtom(result, 0);
	TupleSetAtom(result, 2, z - x);
}

/**
 * Lookup table for service functions.
 * We need this since we cannot store a function pointer directly in void * providerData
 */

#define ADD1_INDEX		0
#define ADD2_INDEX		1
// etc ...

#define N_SERVICES 		2

typedef void (*MathFunction)(void * context, Tuple * arguments);

MathFunction functionTable[N_SERVICES] = {
	&add1,
	&add2,
};

// List of corresponding service atoms (hash identifiers)
Atom mathServices[N_SERVICES];


/**
 * Argument index table, initialized during setup 
 */
#define MAX_N_ARGUMENTS 3
index8 argumentIndexTable[N_SERVICES][MAX_N_ARGUMENTS];

typedef struct s_MathContext {
	MathFunction function;
	bool hasBeenCalled;
} MathContext;


/**
 * Stubs for the service provider
 */
static void serviceSetupContext(void * _context, void * providerData, Tuple * arguments)
{
	MathContext * context = _context;
	index32 functionIndex = (data64) providerData;
	context->function = functionTable[functionIndex];
	context->hasBeenCalled = false;
}


static bool serviceCall(void * _context, Tuple * arguments)
{
	MathContext * context = _context;
	if(context->hasBeenCalled)
		return false;
	context->function(context, arguments);
	context->hasBeenCalled = true;
	return true;
}


static void serviceFinalizeContext(void * context)
{
	// Nothing to do
}


MachineServiceProvider mathServiceProvider = {
	.setupContext = &serviceSetupContext,
	.call = &serviceCall,
	.freeContext = &serviceFinalizeContext,
};


static void setupAdd1(void)
{
	// NOTE: this must be in canonical order and arguments numbered accordingly
	Atom formula = CStringToPredicate("= $INT + @INT + @INT");
	// PrintFormula(formula);
	// PrintChar('\n');
	// PredicateRoleIndex(form, roles[j]);

	MachineService service = {
		.provider = &mathServiceProvider,
		.providerData = ADD1_INDEX,
	};

	Expression addExpression;
	CreateMachineExpression(&addExpression, 3, &service);

	mathServices[ADD1_INDEX] = RegistryAddService(
		FormulaGetForm(formula),
		FormulaGetActors(formula),
		&addExpression
	);

	IFactRelease(formula);
}


static void setupAdd2(void)
{
	Atom formula = CStringToPredicate("= @INT + @INT + $INT");

	MachineService service = {
		.provider = &mathServiceProvider,
		.providerData = (void *) ADD2_INDEX,
	};

	Expression addExpression;
	CreateMachineExpression(&addExpression, 3, &service);

	mathServices[ADD2_INDEX] = RegistryAddService(
		FormulaGetForm(formula),
		FormulaGetActors(formula),
		&addExpression
	);

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
		RegistryRemoveService(mathServices[i]);
}
