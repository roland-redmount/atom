
#include "kernel/ifact.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "lang/formula.h"
#include "library/MachineService.h"
#include "memory/allocator.h"
#include "parser/TermBuilder.h"


/**
 * A MachineServiceData holds the data one registered machine service needs to evaluate itself.
 * It is stored in the Operator.impl.machine.providerData slot.
 */
typedef struct s_MachineServiceData {
	MachineFunction function;
	size8 nArguments;
	// size of the function state, zero for a function computing a single tuple
	size32 stateSize;
	/**
	 * The relation column of each argument of the signature: argumentIndex[i] is the
	 * column of the argument the signature numbered i + 1. The columns are in canonical
	 * role order, unrelated to the order the signature writes its arguments in.
	 * A service with a state also declares this array as its index order;
	 * see RegisterMachineService()
	 */
	index8 argumentIndex[MACHINE_SERVICE_MAX_ARITY];
} MachineServiceData;


/**
 * The context of one evaluation of a machine service. The arguments[] array holds a copy
 * of the operator arguments in the signature ("user") order for the duration of a call;
 * see MachineFunction.
 */
typedef struct s_MachineServiceContext {
	bool hasBeenCalled;
	// set once the function has reported no more tuples, and is not to be called again
	bool isExhausted;
	Atom arguments[MACHINE_SERVICE_MAX_ARITY];
	// the function state, sizes determined by the RegisterMachineService() stateSize argument
	byte state[];
} MachineServiceContext;



static bool machineServiceCall(OperatorContext * context)
{
	MachineServiceContext * serviceContext = (MachineServiceContext *) context->data;
	MachineServiceData const * data = context->op->impl.machine.providerData;

	// A function reporting no more tuples is not called again, and a function with
	// no state computes a single tuple and so is called once
	if(serviceContext->isExhausted || (!data->stateSize && serviceContext->hasBeenCalled))
		return false;
	bool isFirstCall = !serviceContext->hasBeenCalled;
	serviceContext->hasBeenCalled = true;

	// permute the caller's arguments into the signature order the function is written in
	for(index8 i = 0; i < data->nArguments; i++)
		serviceContext->arguments[i] = context->arguments[data->argumentIndex[i]];

	if(!data->function(
		serviceContext->arguments, data->stateSize ? serviceContext->state : 0, isFirstCall)) {
		serviceContext->isExhausted = true;
		return false;
	}

	// permute back, so that the computed arguments reach the caller in column order
	for(index8 i = 0; i < data->nArguments; i++)
		context->arguments[data->argumentIndex[i]] = serviceContext->arguments[i];
	return true;
}


static void machineServiceFinalizeOperator(Operator * op)
{
	Free(op->impl.machine.providerData);
}


/**
 * One provider serves every machine service. The function to call for a specific
 * service is stored in the impl.machine.providerData field of each operator.
 */
static MachineOperatorProvider machineServiceProvider = {
	// nothing to set up: the zeroed context is the state before the first call
	.setupContext = 0,
	.call = &machineServiceCall,
	// nothing to finalize: the context holds no allocation of its own
	.finalizeContext = 0,
	.finalizeOperator = &machineServiceFinalizeOperator
};


/**
 * Read the signature actors into the parameter IO of the service and into the argument
 * index of the function, returning the column types. The actors are in relation column
 * order.
 */
static TypeSignature readSignatureParameters(
	TypedTuple const * signatureActors, MachineServiceData * data, IOSignature * ioSignature)
{
	bool numberSeen[MACHINE_SERVICE_MAX_ARITY];
	SetMemory(numberSeen, sizeof(numberSeen), 0);
	byte atomTypes[MACHINE_SERVICE_MAX_ARITY];
	byte parameterIO[MACHINE_SERVICE_MAX_ARITY];

	for(index8 i = 0; i < data->nArguments; i++) {
		TypedAtom actor = TypedTupleGetElement(signatureActors, i);
		// every actor of a signature is a parameter
		ASSERT(actor.type == AT_PARAMETER)
		ASSERT(actor.atom.parameter.atomType)
		atomTypes[i] = actor.atom.parameter.atomType;
		parameterIO[i] = actor.atom.parameter.io;

		// a signature numbers its arguments 1 to the arity, each exactly once
		index8 number = actor.atom.parameter.number;
		ASSERT((number >= 1) && (number <= data->nArguments))
		ASSERT(!numberSeen[number - 1])
		numberSeen[number - 1] = true;
		data->argumentIndex[number - 1] = i;
	}
	*ioSignature = CreateIOSignature(parameterIO, data->nArguments);
	return CreateTypeSignature(atomTypes, data->nArguments);
}


Service RegisterMachineService(
	char const * signature, MachineFunction function, size32 stateSize)
{
	Atom term = CStringToTerm(signature);
	FormulaView termView = FormulaGetView(term);
	size8 arity = termView.actors->nAtoms;
	ASSERT(arity <= MACHINE_SERVICE_MAX_ARITY)

	MachineServiceData * data = Allocate(sizeof(MachineServiceData));
	data->function = function;
	data->nArguments = arity;
	data->stateSize = stateSize;

	IOSignature ioSignature;
	TypeSignature typeSignature = readSignatureParameters(termView.actors, data, &ioSignature);

	// A machine service is computed, and so has no tuple storage: the relation exists
	// only to name the signature the service is registered under, and is removed with the
	// last service naming it; see ReleaseRelation()
	Relation const * relation = FindOrCreateRelation(termView.form, arity, typeSignature);

	// A function with no state yields at most one tuple, and so declares no index order.
	// A function with a state declares the order its signature writes its arguments in;
	// see the ordering contract in operator.h
	index8 const * indexOrder = stateSize ? data->argumentIndex : 0;
	Operator * op = CreateMachineOperator(
		arity, indexOrder, &machineServiceProvider, data,
		sizeof(MachineServiceContext) + stateSize);
	Service service = CreateService(relation, ioSignature, op, SERVICE_PRIMITIVE);
	ReleaseRelation(relation);
	ReleaseFormula(term);
	return service;
}


void FreeMachineServices(void)
{
	// Remove all services registered by machineServiceProvider.
	// NOTE: this is highly inefficient, but typically only called prior to kernel shutdown.
	Service service;
	while(ServiceRegistryFindByMachineProvider(&machineServiceProvider, &service))
		RemoveService(service.relation, service.op);
}
