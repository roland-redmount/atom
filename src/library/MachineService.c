
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
 * What one registered machine service needs to evaluate itself, held as the
 * providerData of its machine operator. A function pointer is stored in this
 * structure rather than in providerData directly, which is a void * and so cannot
 * portably hold one.
 */
typedef struct s_MachineServiceData {
	MachineFunction function;
	size8 nArguments;
	// what the function keeps between calls, zero for one computing a single tuple
	size32 stateSize;
	/**
	 * The relation column of each argument of the signature: argumentIndex[i] is the
	 * column holding the argument the signature numbered i + 1. Columns are in the
	 * canonical role order of the form, which is an order of role name hashes and so
	 * unrelated to the order the signature was written in.
	 *
	 * This is also the index order a service with a state declares, being a permutation
	 * of the columns; see RegisterMachineService()
	 */
	index8 argumentIndex[MACHINE_SERVICE_MAX_ARITY];
} MachineServiceData;


/**
 * The context of one evaluation of a machine service. The arguments are held here in
 * signature order for the duration of a call, since the function is written in that
 * order while the caller's tuple is in column order.
 */
typedef struct s_MachineServiceContext {
	// whether the function has been called for this evaluation
	bool hasBeenCalled;
	// whether it has said it has no more tuples, after which it is not called again
	bool isExhausted;
	Atom arguments[MACHINE_SERVICE_MAX_ARITY];
	// The state of the function, of the size its service was registered with.
	// createChildContext() zeroes the whole context, so it starts zeroed.
	byte state[];
} MachineServiceContext;


// The relations created by RegisterMachineService(), which FreeMachineServices() removes
static ResizingArray registeredRelations = {0};


static bool machineServiceCall(OperatorContext * context)
{
	MachineServiceContext * serviceContext = (MachineServiceContext *) context->data;
	MachineServiceData const * data = context->op->impl.machine.providerData;

	// The function is not called again once it has said it has no more tuples.
	// A function with no state computes a single tuple, so one call is all it gets.
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
	// nothing to set up: the context is zeroed, which is the state before the first call
	.setupContext = 0,
	.call = &machineServiceCall,
	// nothing to finalize: the context holds no allocation of its own
	.finalizeContext = 0,
	.finalizeOperator = &machineServiceFinalizeOperator
};


/**
 * Read the signature actors, which are in relation column order, into the column types
 * and parameter IO of the service and the argument index of the function.
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
 * Find the relation of a signature, or create and register it if this is the first
 * service registered for it. A computed relation has no storage provider, and no
 * particular index column order.
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

	/**
	 * A function with no state computes a single tuple, and so declares no index order,
	 * which is what the operator contract asks of an operator yielding at most one tuple.
	 * One with a state may yield several, and declares the order its signature writes its
	 * arguments in; the argument index is that order, being a permutation of the columns.
	 */
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

	// Remove in reverse order of registration, so that a relation is removed before
	// anything registered before it that it might depend on.
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
