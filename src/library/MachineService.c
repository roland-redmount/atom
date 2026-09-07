
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
 * TODO: can we just move this stuff to Operator then?
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
	 * 
	 * NOTE: Operator has this as well -- the only difference seems to be that
	 * Operator.indexOrder is nullable, and this is not.
	 */
	index8 argumentIndex[RELATION_MAX_ARITY];
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
	Atom arguments[RELATION_MAX_ARITY];
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
 * 
 * TODO: what if functions need access to some form of storage, for example
 * precomputed math tables, constants &c ? Such data does not belong in the
 * function's state; it needs a storage pointer in MachineServiceData, similar
 * to RelationBTreeOperatorData.
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
 * Read the given parameters into the parameter IO of the service and into the argument
 * index of the function, returning the column types. The parameters are in canonical order.
 */
static TypeSignature readSignatureParameters(
	TypedTuple const * parameters, index8 argumentIndex[], IOSignature * ioSignature)
{
	bool numberSeen[RELATION_MAX_ARITY] = {0};
	byte atomTypes[RELATION_MAX_ARITY];
	byte parameterIO[RELATION_MAX_ARITY];

	for(index8 i = 0; i < parameters->nAtoms; i++) {
		TypedAtom actor = TypedTupleGetElement(parameters, i);
		// every actor must be a parameter
		ASSERT(actor.type == AT_PARAMETER)
		ASSERT(actor.atom.parameter.atomType)
		atomTypes[i] = actor.atom.parameter.atomType;
		parameterIO[i] = actor.atom.parameter.io;

		// a signature numbers its arguments 1 ... arity, each number occurs exactly once
		index8 number = actor.atom.parameter.number;
		index8 index = number - 1;
		ASSERT((number >= 1) && (number <= parameters->nAtoms))
		ASSERT(!numberSeen[index])
		numberSeen[index] = true;
		argumentIndex[index] = i;
	}
	*ioSignature = CreateIOSignature(parameterIO, parameters->nAtoms);
	return CreateTypeSignature(atomTypes, parameters->nAtoms);
}


Service RegisterMachineService(
	char const * signature, MachineFunction function, size32 stateSize)
{
	// Parse the signature
	Atom term = CStringToTerm(signature);
	FormulaView termView = FormulaGetView(term);
	size8 arity = termView.actors->nAtoms;
	ASSERT(arity <= RELATION_MAX_ARITY)

	// NOTE: this could be pool allocated
	MachineServiceData * data = Allocate(sizeof(MachineServiceData));
	data->function = function;
	data->nArguments = arity;
	data->stateSize = stateSize;

	IOSignature ioSignature;
	TypeSignature typeSignature = readSignatureParameters(
		termView.actors, data->argumentIndex, &ioSignature);

	// A machine service is computed, and so has no tuple storage: the relation exists
	// only to name the signature the service is registered under, and is removed with the
	// last service naming it; see ReleaseRelation()
	// NOTE: I think the state here plays the same role as the storage pointer for a
	// primitive service registers by RelationTable.
	Relation relation = CreateRelation(termView.form, typeSignature);

	// A function with no state yields at most one tuple, and so declares no index order.
	// A function with a state declares the order its signature writes its arguments in;
	// see the ordering contract in operator.h
	index8 const * indexOrder = stateSize ? data->argumentIndex : 0;
	Operator * op = CreateMachineOperator(
		arity, indexOrder, &machineServiceProvider, data,
		sizeof(MachineServiceContext) + stateSize);
	Service service = CreateService(relation, ioSignature, op);
	ReleaseRelation(relation);
	ReleaseFormula(term);
	return service;
}


void FreeMachineServices(void)
{
	// Remove all services registered by machineServiceProvider.
	// NOTE: this is highly inefficient, but typically only called prior to kernel shutdown.
	Service service;
	while(FindServiceByMachineProvider(&machineServiceProvider, &service))
		RemoveService(service.relation, service.op);
}
