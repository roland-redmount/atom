
#include "kernel/service.h"
#include "memory/allocator.h"


/**
 * Create a Service and setup common fields.
 * The caller obtains a reference to the created service.
 */
static Service * createService(enum ServiceType type, size8 nArguments, size32 contextSize)
{
	Service * service = Allocate(sizeof(Service));
	SetMemory(service, sizeof(Service), 0);
	service->type = type;
	service->nArguments = nArguments;
	service->referenceCount = 1;
	service->contextSize = contextSize;
	return service;
}

//------------------------------------- SERVICE_PERMUTE -----------------------------------------

typedef struct s_PermuteContext {
	Tuple * childArguments;
	ServiceContext * childContext;
} PermuteContext;


Service * CreatePermuteService(
	size8 nArguments, Tuple const * constants, index8 const * argumentMap, Service * childService)
{
	Service * service = createService(SERVICE_PERMUTE, nArguments, sizeof(PermuteContext));
	service->impl.permute.childService = childService;
	AcquireService(childService);
	service->impl.permute.constants = constants ? CreateTupleFromTuple(constants) : 0;
	service->impl.permute.argumentMap = Allocate(childService->nArguments);
	CopyMemory(argumentMap, service->impl.permute.argumentMap, childService->nArguments);
	return service;
}


static void permuteServiceSetupContext(ServiceContext * context)
{
	PermuteContext * permuteContext = (PermuteContext *) &context->data;
	size8 nChildArguments = context->service->impl.permute.childService->nArguments;
	
	permuteContext->childArguments = CreateTuple(nChildArguments);
	for(index8 i = 0, k = 0; i < nChildArguments; i++) {
		int parentIndex = context->service->impl.permute.argumentMap[i];
		TypedAtom a;
		if(parentIndex) {
			// copy parent argument to child, permuted
			a = TupleGetElement(context->arguments, parentIndex - 1);
		}
		else {
			// copy next constant
			a = TupleGetElement(context->service->impl.permute.constants, k++);
		}
		TupleSetElement(permuteContext->childArguments,	i, a);
	}
	// setup child context
	permuteContext->childContext = ServiceCreateContext(
		context->service->impl.permute.childService,
		permuteContext->childArguments
	);
}


static bool permuteServiceCall(ServiceContext * context)
{
	PermuteContext * permuteContext = (PermuteContext *) &context->data;
	bool success = ServiceCall(permuteContext->childContext);
	if(success) {
		size8 nChildArguments = context->service->impl.permute.childService->nArguments;
		// copy child result tuple back to parent arguments, permuted
		for(index8 i = 0; i < nChildArguments; i++) {
			int parentIndex = context->service->impl.permute.argumentMap[i];
			if(parentIndex) {
				// copy parent argument to child, permuted
				TupleSetElement(
					context->arguments,
					parentIndex - 1,
					TupleGetElement(permuteContext->childArguments, i)
				);
			}
		}
	}
	return success;
}


static void teardownPermuteService(Service * service)
{
	ASSERT(service->type == SERVICE_PERMUTE)
	ReleaseService(service->impl.permute.childService);
	if(service->impl.permute.constants)
		FreeTuple(service->impl.permute.constants);
	Free(service->impl.permute.argumentMap);
}


static void permuteServiceFinalizeContext(ServiceContext * context)
{
	PermuteContext * permuteContext = (PermuteContext *) &context->data;
	ServiceFreeContext(permuteContext->childContext);
	FreeTuple(permuteContext->childArguments);
}


//------------------------------------- SERVICE_JOIN -----------------------------------------

typedef struct s_JoinContext {
	// Copy of the caller's arguments
	Tuple * argumentsCopy;
	// Left and right child contexts
	ServiceContext * leftContext;
	ServiceContext * rightContext;
} JoinContext;


Service * CreateJoinService(Service * leftChild, Service * rightChild)
{
	ASSERT(leftChild->nArguments == rightChild->nArguments)
	Service * service = createService(SERVICE_JOIN, leftChild->nArguments, sizeof(JoinContext));
	service->impl.join.left = leftChild;
	AcquireService(leftChild);
	service->impl.join.right = rightChild;
	AcquireService(rightChild);
	return service;
}


static void teardownJoinService(Service * service)
{
	ASSERT(service->type == SERVICE_JOIN)
	ReleaseService(service->impl.join.left);
	ReleaseService(service->impl.join.right);
}


/**
 * Obtain a tuple from the left child service of a join service
 * and setup the right child service context for evaluation.
 */
static bool joinServiceEvaluateLeft(ServiceContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	ASSERT(joinContext->leftContext)
	// restore parent arguments tuple
	CopyTuples(joinContext->argumentsCopy, context->arguments);

	// Obtain next tuple from left service, if any.
	// This will write directly to the context->arguments tuple
	if(!ServiceCall(joinContext->leftContext)) {
		// no more tuples, free left child context
		ServiceFreeContext(joinContext->leftContext);
		joinContext->leftContext = 0;
		return false;
	}
	// start new evaluation of right service
	joinContext->rightContext = ServiceCreateContext(
		context->service->impl.join.right,
		context->arguments
	);
	return true;
}


static void joinServiceSetupContext(ServiceContext * context)
{
	JoinContext * joinContext = (JoinContext *) &context->data;
	// Make a backup of the argument tuple
	joinContext->argumentsCopy = CreateTupleFromTuple(context->arguments);
	// call left service to prepare for iteration
	joinContext->leftContext = ServiceCreateContext(
		context->service->impl.join.left,
		context->arguments
	);
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
	ASSERT(joinContext->rightContext)

	// attempt to obtain next tuple from right child serice
	while(!ServiceCall(joinContext->rightContext)) {
		// no more tuples from right service, start over with new left tuple
		ServiceFreeContext(joinContext->rightContext);
		joinContext->rightContext = 0;
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


//------------------------------------- SERVICE_MACHINE -----------------------------------------


static void machineServiceSetupContext(ServiceContext * context)
{
	context->service->impl.machine.provider->setupContext(context);
}


static bool machineServiceCall(ServiceContext * context)
{
	return context->service->impl.machine.provider->call(context);
}


static void machineServiceFinalizeContext(ServiceContext * context)
{
	context->service->impl.machine.provider->finalizeContext(context);
}


Service * CreateMachineService(size8 nArguments, MachineServiceProvider * provider, void * providerData)
{
	Service * service = createService(SERVICE_MACHINE, nArguments, provider->contextSize);
	service->impl.machine.provider = provider;
	service->impl.machine.providerData = providerData;
	return service;
}

//------------------------------------- Generic Service -----------------------------------------


void AcquireService(Service * service)
{
	service->referenceCount++;
}


void ReleaseService(Service * service)
{
	service->referenceCount--;
	if(service->referenceCount == 0) {
		switch(service->type) {
		case SERVICE_PERMUTE:
			teardownPermuteService(service);
			break;

		case SERVICE_JOIN:
			teardownJoinService(service);
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
			// Nothing to do (?)
			break;
		
		default:
			ASSERT(false)
			break;
		}
		Free(service);
	}
}


ServiceContext * ServiceCreateContext(Service const * service, Tuple * arguments)
{
	size32 contextSize = sizeof(ServiceContext) + service->contextSize;
	ServiceContext * context = Allocate(contextSize);
	SetMemory(context, contextSize, 0);
	context->service = service;
	context->arguments = arguments;
	
	switch(service->type) {
	case SERVICE_PERMUTE:
		permuteServiceSetupContext(context);
		break;

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
	case SERVICE_PERMUTE:
		return permuteServiceCall(context);

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
	case SERVICE_PERMUTE:
		permuteServiceFinalizeContext(context);
		break;

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
	case SERVICE_PERMUTE:
		PrintCString("PERMUTE");
		break;
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
