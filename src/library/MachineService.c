
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
	/**
	 * The relation column of each argument of the signature: argumentIndex[i] is the
	 * column holding the argument the signature numbered i + 1. Columns are in the
	 * canonical role order of the form, which is an order of role name hashes and so
	 * unrelated to the order the signature was written in.
	 */
	index8 argumentIndex[MACHINE_SERVICE_MAX_ARITY];
} MachineServiceData;


/**
 * The context of an executing machine service. The arguments are held here in
 * signature order for the duration of a call, since the function is written in that
 * order while the caller's tuple is in column order.
 */
typedef struct s_MachineServiceContext {
	bool hasBeenCalled;
	Atom arguments[MACHINE_SERVICE_MAX_ARITY];
} MachineServiceContext;


// The relations created by RegisterMachineService(), which FreeMachineServices() removes
static ResizingArray registeredRelations = {0};


static void machineServiceSetupContext(OperatorContext * context)
{
	MachineServiceContext * serviceContext = (MachineServiceContext *) context->data;
	serviceContext->hasBeenCalled = false;
}


static bool machineServiceCall(OperatorContext * context)
{
	MachineServiceContext * serviceContext = (MachineServiceContext *) context->data;
	// a machine function computes at most one tuple
	if(serviceContext->hasBeenCalled)
		return false;
	serviceContext->hasBeenCalled = true;

	MachineServiceData const * data = context->op->impl.machine.providerData;
	// permute the caller's arguments into the signature order the function is written in
	for(index8 i = 0; i < data->nArguments; i++)
		serviceContext->arguments[i] = context->arguments[data->argumentIndex[i]];

	if(!data->function(serviceContext->arguments))
		return false;

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
	.setupContext = &machineServiceSetupContext,
	.call = &machineServiceCall,
	// nothing to finalize: the context holds no allocation of its own
	.finalizeContext = 0,
	.finalizeOperator = &machineServiceFinalizeOperator,
	.contextSize = sizeof(MachineServiceContext)
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


Service RegisterMachineService(char const * signature, MachineFunction function)
{
	Formula * term = CStringToTerm(signature);
	size8 arity = term->actors->nAtoms;
	ASSERT(arity <= MACHINE_SERVICE_MAX_ARITY)

	MachineServiceData * data = Allocate(sizeof(MachineServiceData));
	data->function = function;
	data->nArguments = arity;

	byte atomTypes[MACHINE_SERVICE_MAX_ARITY];
	byte parameterIO[MACHINE_SERVICE_MAX_ARITY];
	readSignatureParameters(term, data, atomTypes, parameterIO);

	RelationTable const * relation = findOrCreateRelation(term->form, arity, atomTypes);

	// A machine function computes at most one tuple, and so declares no index order
	Operator * op = CreateMachineOperator(arity, 0, &machineServiceProvider, data);
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
