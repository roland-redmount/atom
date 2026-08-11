
#include "kernel/ifact.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "library/MachineService.h"
#include "memory/allocator.h"
#include "parser/TermBuilder.h"
#include "util/ResizingArray.h"


/**
 * What one registered machine service needs to evaluate itself, held as the providerData
 * of its machine operator. The function pointer is stored here rather than in
 * providerData, a void * being unable to hold a function pointer portably.
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
 * The context of one evaluation of a machine service. The arguments are held here in
 * signature order for the duration of a call; see MachineFunction.
 * The arguments buffer has a fixed size, so that the state can be the flexible member.
 */
typedef struct s_MachineServiceContext {
	bool hasBeenCalled;
	// set once the function has reported no more tuples, and is not to be called again
	bool isExhausted;
	Atom arguments[MACHINE_SERVICE_MAX_ARITY];
	// the function state, of the size RegisterMachineService() was given
	byte state[];
} MachineServiceContext;


// The relations created by RegisterMachineService(), which FreeMachineServices() removes
static ResizingArray registeredRelations = {0};


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
 * One provider serves every machine service, the function to call being part of the
 * providerData of each operator.
 */
static MachineProvider machineServiceProvider = {
	// nothing to set up: the zeroed context is the state before the first call
	.setupContext = 0,
	.call = &machineServiceCall,
	// nothing to finalize: the context holds no allocation of its own
	.finalizeContext = 0,
	.finalizeOperator = &machineServiceFinalizeOperator
};


/**
 * Read the signature actors into the column types and parameter IO of the service,
 * and into the argument index of the function. The actors are in relation column order.
 */
static void readSignatureParameters(
	Formula const * signature, MachineServiceData * data, byte atomTypes[], byte parameterIO[])
{
	bool numberSeen[MACHINE_SERVICE_MAX_ARITY];
	SetMemory(numberSeen, sizeof(numberSeen), 0);

	for(index8 i = 0; i < data->nArguments; i++) {
		TypedAtom actor = TypedTupleGetElement(signature->actors, i);
		// every actor of a signature is a parameter
		ASSERT(actor.type == AT_PARAMETER)
		atomTypes[i] = actor.atom.parameter.atomType;
		ASSERT(atomTypes[i])
		parameterIO[i] = actor.atom.parameter.io;

		// a signature numbers its arguments 1 to the arity, each exactly once
		index8 number = actor.atom.parameter.number;
		ASSERT((number >= 1) && (number <= data->nArguments))
		ASSERT(!numberSeen[number - 1])
		numberSeen[number - 1] = true;
		data->argumentIndex[number - 1] = i;
	}
}


/**
 * Find the relation of a signature, or create and register it for the first service
 * registered. A computed relation has no storage provider and no index column order.
 */
static RelationTable const * findOrCreateRelation(
	Atom termForm, size8 arity, byte const atomTypes[])
{
	RelationTable const * relation = RelationRegistryFind(termForm, arity, atomTypes);
	if(relation)
		return relation;

	relation = CreateRelationTable(0, termForm, arity, atomTypes, 0);
	RelationRegistryAdd(relation);
	if(!registeredRelations.elementSize)
		CreateResizingArray(&registeredRelations, sizeof(RelationTable const *), 16);
	ResizingArrayAppend(&registeredRelations, &relation);
	return relation;
}


Service RegisterMachineService(
	char const * signature, MachineFunction function, size32 stateSize)
{
	Formula * term = CStringToTerm(signature);
	size8 arity = term->actors->nAtoms;
	ASSERT(arity <= MACHINE_SERVICE_MAX_ARITY)

	MachineServiceData * data = Allocate(sizeof(MachineServiceData));
	data->function = function;
	data->nArguments = arity;
	data->stateSize = stateSize;

	byte atomTypes[MACHINE_SERVICE_MAX_ARITY];
	byte parameterIO[MACHINE_SERVICE_MAX_ARITY];
	readSignatureParameters(term, data, atomTypes, parameterIO);

	RelationTable const * relation = findOrCreateRelation(term->form, arity, atomTypes);

	// A function with no state yields at most one tuple, and so declares no index order.
	// A function with a state declares the order its signature writes its arguments in;
	// see the ordering contract in operator.h
	Operator * op = CreateMachineOperator(
		arity, stateSize ? data->argumentIndex : 0, &machineServiceProvider, data,
		sizeof(MachineServiceContext) + stateSize);
	Service service = ServiceRegistryAdd(relation, parameterIO, op);
	// the service registry now holds the reference to the operator
	ReleaseOperator(op);
	FreeFormula(term);
	return service;
}


void FreeMachineServices(void)
{
	// nothing to do if no service was ever registered
	if(!registeredRelations.elementSize)
		return;

	// remove in reverse order of registration
	size32 nRelations = ResizingArrayNElements(&registeredRelations);
	while(nRelations > 0) {
		RelationTable const * relation =
			*((RelationTable const **) ResizingArrayGetElement(&registeredRelations, --nRelations));
		ServiceRegistryRemoveAll(relation);
		RelationRegistryRemove(relation);
	}
	FreeResizingArray(&registeredRelations);
	SetMemory(&registeredRelations, sizeof(ResizingArray), 0);
}
