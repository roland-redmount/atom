
#include "kernel/service.h"
#include "memory/allocator.h"


/**
 * Setup the common part of an Service
 */
static void setupService(
	Service * service, enum ServiceType type, size8 nArguments, index8 const * argumentMap)
{
	SetMemory(service, sizeof(Service), 0);
	service->type = type;
	ASSERT(nArguments <= 8);	// due to fixed argument map array size
	service->dimensions.nArguments = nArguments;
	if(argumentMap)
		CopyMemory(argumentMap, &(service->argumentMap), nArguments);
	else {
		for(index8 i = 0; i < nArguments; i++)
			service->argumentMap[i] = i;
	}
}


void SetupMachineService(
	Service * service, size8 nArguments, index8 const * argumentMap, MachineService const * machineService)
{
	setupService(service, SERVICE_MACHINE, nArguments, argumentMap);
	service->dimensions.contextSize = machineService->contextSize;
	service->value.machineService = *machineService;
}


typedef struct s_JoinContext {
	Tuple * argumentsCopy;
	ServiceContext * leftContext;
	ServiceContext * rightContext;
} JoinContext;


void SetupJoinService(
	Service * service, size8 nArguments, index8 const * argumentMap,
	Service const * leftChild, Service const * rightChild)
{
	setupService(service, SERVICE_JOIN, nArguments, argumentMap);
	service->dimensions.contextSize = sizeof(JoinContext);
	service->value.children.left = leftChild;
	service->value.children.right = rightChild;
}


/**
 * Obtain a tuple from the left child service of a join service
 * and setup the right child service context for evaluation.
 */
static bool joinServiceEvaluateLeft(ServiceContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	// restore arguments tuple
	CopyTuples(joinContext->argumentsCopy, context->arguments);

	// Obtain next tuple from left service, if any.
	// This will write directly to the context->arguments tuple
	ASSERT(joinContext->leftContext)
	if(!ServiceCall(joinContext->leftContext)) {
		// no more tuples, free child context
		ServiceFreeContext(joinContext->leftContext);
		joinContext->leftContext = 0;
		joinContext->rightContext = 0;
		return false;
	}
	// start new evaluation of right service
	Service const * right = context->service->value.children.right;
	joinContext->rightContext = ServiceCreateContext(right, context->arguments);
	return true;
}


static void joinServiceSetupContext(ServiceContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	// For the right context, we must create a new context for each tuple from the left,
	// and so we need to store a copy of the query tuple to prevent overwriting it.
	joinContext->argumentsCopy = CreateTupleFromTuple(context->arguments);
	// call left service to prepare for iteration
	Service const * left = context->service->value.children.left;
	joinContext->leftContext = ServiceCreateContext(left, context->arguments);
	joinServiceEvaluateLeft(context);
}


static bool joinServiceCall(ServiceContext * context)
{
	/**
	 * Each call to a join service gives one tuple from the Carthesian product
	 * of the left and right relations, constrained on any shared variables.
	 * For each tuple from the left relation, we enumerate all tuples from the right relation 
	 * (if any) and keep those that unify with the left tuple. So we perform a new
	 * query for the right relation for each tuple from the left relation.
	 * NOTE: this is more efficient if the left relation is smaller than the right,
	 * and the right relation has efficient lookup (not table-scanning).
	 */
	JoinContext * joinContext = (JoinContext *) &context->data;
	if(!joinContext->leftContext)
		return false;

	// attempt to obtain next tuple from right child serice
	while(!ServiceCall(joinContext->rightContext)) {
		// no more tuples from right service, start over with new left tuple
		ServiceFreeContext(joinContext->rightContext);
		if(!joinServiceEvaluateLeft(context)) {
			// join iteration is complete
			return false;
		}
	}
	// yield the resulting tuple
	return true;
}


static void joinServiceFinalizeContext(ServiceContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	if(joinContext->leftContext)
		ServiceFreeContext(joinContext->leftContext);
	if(joinContext->rightContext)
		ServiceFreeContext(joinContext->rightContext);
	FreeTuple(joinContext->argumentsCopy);
}

/**
 * Machine service
 */
static void machineServiceSetupContext(ServiceContext * context)
{
	MachineService const * service = &context->service->value.machineService;
	service->provider->setupContext(context, service->providerData);
}


static bool machineServiceCall(ServiceContext * context)
{
	MachineService const * service = &context->service->value.machineService;
	return service->provider->call(context);
}


static void machineServiceFinalizeContext(ServiceContext * context)
{
	MachineService const * service = &context->service->value.machineService;
	service->provider->finalizeContext(context);
}


ServiceContext * ServiceCreateContext(Service const * service, Tuple * arguments)
{
	size32 contextSize = sizeof(ServiceContext) + service->dimensions.contextSize;
	ServiceContext * context = Allocate(contextSize);
	SetMemory(context, contextSize, 0);
	context->service = service;
	context->arguments = arguments;
	
	switch(service->type) {
	case SERVICE_JOIN:
		joinServiceSetupContext(context);
		break;

	case SERVICE_UNION:
		// TODO
		ASSERT(false)
		break;

	case SERVICE_PROJECT:
		// TODO
		ASSERT(false)
		break;

	case SERVICE_MACHINE:
		machineServiceSetupContext(context);
		break;
	
	default:
		ASSERT(false)
		break;
	}
	return context;
}


bool ServiceCall(ServiceContext * context)
{
	switch(context->service->type) {
	case SERVICE_JOIN:
		return joinServiceCall(context);

	case SERVICE_UNION:
		// TODO
		ASSERT(false)
		return false;

	case SERVICE_PROJECT:
		// TODO
		ASSERT(false)
		return false;

	case SERVICE_MACHINE:
		return machineServiceCall(context);
	
	default:
		ASSERT(false)
		return false;
	}
}


void ServiceFreeContext(ServiceContext * context)
{
	switch(context->service->type) {
	case SERVICE_JOIN:
		joinServiceFinalizeContext(context);
		break;

	case SERVICE_UNION:
		// TODO
		ASSERT(false)
		break;

	case SERVICE_PROJECT:
		// TODO
		ASSERT(false)
		break;

	case SERVICE_MACHINE:
		machineServiceFinalizeContext(context);
		break;
	
	default:
		ASSERT(false)
		break;
	}
	Free(context);
}


void PrintService(Service const * service)
{
	switch(service->type) {
	case SERVICE_JOIN:
		PrintCString("JOIN");
		break;
	case SERVICE_UNION:
		PrintCString("UNION");
		break;
	case SERVICE_PROJECT:
		PrintCString("PROJECT");
		break;
	case SERVICE_MACHINE:
		PrintCString("MACHINE");
		break;
	default:
		ASSERT(false);
		break;
	}
}


TypedAtom ServiceContextReadArgument(ServiceContext * context, index8 index)
{
	index8 const * argumentMap = context->service->argumentMap;
	return TupleGetElement(context->arguments, argumentMap[index]);
}


void ServiceContextWriteArgument(ServiceContext * context, index8 index, TypedAtom argument)
{
	index8 const * argumentMap = context->service->argumentMap;
	TupleSetElement(context->arguments, argumentMap[index], argument);
}
