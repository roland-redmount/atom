#include "kernel/Parameter.h"
#include "kernel/operator.h"
#include "kernel/ifact.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/math.h"
#include "memory/allocator.h"
#include "parser/PredicateBuilder.h"


// relations
#define RELATION_ADD	0
#define RELATION_MUL	1

#define N_RELATIONS		2

static size8 mathRelationArity[N_RELATIONS] = {
	3,		// ADD1
	3,		// MUL1
};

#define MAX_RELATION_ARITY	3

// List of created relation tables
// These are needed by MathTeardown()
RelationTable const * mathRelations[N_RELATIONS];

// Precomputed argument indexes, such that relationArgumentIndex[i][j] is
// the canonical index of "user order" argument j for service relation i.
// TODO: This is similar to kernel.corePredicateRoleIndex,
// we should have a more general mechanism for handling "user order".
static index8 relationArgumentIndex[N_RELATIONS][MAX_RELATION_ARITY];

// services
#define SERVICE_ADD1		0
#define SERVICE_ADD2		1
#define SERVICE_MUL1		2

#define N_SERVICES 		3

// Parameter IO for each service
static byte mathParameterIO[N_SERVICES][MAX_RELATION_ARITY] = {
	// ADD1
	{PARAMETER_IN, PARAMETER_IN, PARAMETER_OUT},
	// ADD2
	{PARAMETER_IN, PARAMETER_OUT, PARAMETER_IN},
	// MUL1
	{PARAMETER_IN, PARAMETER_IN, PARAMETER_OUT},
};

// Relation index for search service
static index32 mathServiceRelation[N_SERVICES] = {
	RELATION_ADD,		// ADD1
	RELATION_ADD,		// ADD2
	RELATION_MUL,		// MUL1
};

/**
 * The operator (+ x<INT + y<INT = z>INT )
 */
static void add1(OperatorContext * context)
{
	index8 * argumentIndex = relationArgumentIndex[RELATION_ADD];
	int64 x = context->arguments[argumentIndex[0]]._int;
	int64 y = context->arguments[argumentIndex[1]]._int;
	context->arguments[argumentIndex[2]]._int = x + y;
}

/**
 * The operator (+ x<INT + y>INT = z<INT)
 * This implements subtraction by solving the equation
 * z = x + y  <->  y = z - x
 */
static void add2(OperatorContext * context)
{
	index8 * argumentIndex = relationArgumentIndex[RELATION_ADD];
	int64 x = context->arguments[argumentIndex[0]]._int;
	int64 z = context->arguments[argumentIndex[2]]._int;
	context->arguments[argumentIndex[1]]._int = z - x;
}

/**
 * The operator (* x<INT * y<INT = z>INT )
 */
static void mul1(OperatorContext * context)
{
	index8 * argumentIndex = relationArgumentIndex[RELATION_ADD];
	int64 x = context->arguments[argumentIndex[0]]._int;
	int64 y = context->arguments[argumentIndex[1]]._int;
	context->arguments[argumentIndex[2]]._int = x * y;
}


/**
 * Lookup table for operator functions.
 * We need this since we cannot store a function pointer directly in void * providerData
 * (due to text/data segment issues)
 */
typedef void (*MathFunction)(OperatorContext * context);

typedef struct s_MathContext {
	MathFunction function;
	bool hasBeenCalled;
} MathContext;

MathFunction functionTable[N_SERVICES] = {
	&add1,
	&add2,
	&mul1
};


/**
 * Stubs for the operator provider
 */
static void mathSetupContext(OperatorContext * context)
{
	MathContext * mathContext = (MathContext *) &context->data;
	index32 functionIndex = (data64) context->op->impl.machine.providerData;
	mathContext->function = functionTable[functionIndex];
	mathContext->hasBeenCalled = false;
}


static bool mathCall(OperatorContext * context)
{
	MathContext * mathContext = (MathContext *) &context->data;
	if(mathContext->hasBeenCalled)
		return false;
	mathContext->function(context);
	mathContext->hasBeenCalled = true;
	return true;
}


static void mathFinalizeContext(OperatorContext * context)
{
	// Nothing to do
}


MachineProvider mathProvider = {
	.setupContext = &mathSetupContext,
	.call = &mathCall,
	.finalizeContext = &mathFinalizeContext,
};


static void registerMathService(index32 serviceIndex)
{
	index32 relationIndex = mathServiceRelation[serviceIndex];
	Operator * op = CreateMachineOperator(
		mathRelationArity[relationIndex],
		0,	// indexOrder, assuming single tuple relations for now
		&mathProvider,
		(void *) ((data64) serviceIndex)
	);
	ServiceRegistryAdd(
		mathRelations[relationIndex],
		mathParameterIO[serviceIndex],
		op
	);
	ReleaseOperator(op);
}

void MathSetup(void)
{
	// Create forms and setup argument indices
	Atom plus = CreateNameFromCString("+");
	Atom times = CreateNameFromCString("*");
	Atom equals = CreateNameFromCString("=");

	Atom addForm = CreatePredicateForm((Atom[]) {plus, plus, equals}, 3);
	Atom mulForm = CreatePredicateForm((Atom[]) {times, times, equals}, 3);
	// a relation table is keyed by a term form; these relations are not negated
	Atom addTermForm = CreateTermForm(addForm, true);
	Atom mulTermForm = CreateTermForm(mulForm, true);

	byte atomTypes[3];
	index8 * argumentIndex;

	// create (+ + =) relation table

	// Map roles in our "user order" (+ + =) to canonical order
	argumentIndex = relationArgumentIndex[RELATION_ADD];
	argumentIndex[0] = PredicateRoleIndex(addForm, plus);
	argumentIndex[1] = argumentIndex[0] + 1;
	argumentIndex[2] = PredicateRoleIndex(addForm, equals);

	atomTypes[argumentIndex[0]] = AT_INT;
	atomTypes[argumentIndex[1]] = AT_INT;
	atomTypes[argumentIndex[2]] = AT_INT;
	// NOTE: no particular column index order here
	mathRelations[RELATION_ADD] = CreateRelationTable(0, addTermForm, 3, atomTypes, 0);
	// the table must be registered for dispatch to find it
	RelationRegistryAdd(mathRelations[RELATION_ADD]);

	// create (* * =) relation table

	// Map roles in our "user order" (+ + =) to canonical order
	argumentIndex = relationArgumentIndex[RELATION_MUL];
	argumentIndex[0] = PredicateRoleIndex(mulForm, times);
	argumentIndex[1] = argumentIndex[0] + 1;
	argumentIndex[2] = PredicateRoleIndex(mulForm, equals);

	atomTypes[argumentIndex[0]] = AT_INT;
	atomTypes[argumentIndex[1]] = AT_INT;
	atomTypes[argumentIndex[2]] = AT_INT;
	// NOTE: no particular column index order here
	mathRelations[RELATION_MUL] = CreateRelationTable(0, mulTermForm, 3, atomTypes, 0);
	// the table must be registered for dispatch to find it
	RelationRegistryAdd(mathRelations[RELATION_MUL]);

	// setup operators
	for(index32 serviceIndex = 0; serviceIndex < N_SERVICES; serviceIndex++)
		registerMathService(serviceIndex);

	// the relation tables now hold the references to their forms
	IFactRelease(addTermForm);
	IFactRelease(mulTermForm);
	IFactRelease(addForm);
	IFactRelease(mulForm);
	NameRelease(plus);
	NameRelease(times);
	NameRelease(equals);
}


void MathTeardown(void)
{
	for(index32 i = 0; i < N_RELATIONS; i++) {
		ServiceRegistryRemoveAll(mathRelations[i]);
		RelationRegistryRemove(mathRelations[i]);
	}
}
