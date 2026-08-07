
#include "btree/btree.h"
#include "kernel/service.h"
#include "kernel/tuple.h"
#include "memory/allocator.h"
#include "util/utilities.h"


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
	Atom * childArguments;
	ServiceContext * childContext;
} PermuteContext;


Service * CreatePermuteService(
	size8 nArguments, Atom const * constants, byte const * constantTypes,
	index8 const * argumentMap, Service * childService)
{
	Service * service = createService(SERVICE_PERMUTE, nArguments, sizeof(PermuteContext));
	service->impl.permute.childService = childService;
	AcquireService(childService);

	// Count the constants, and bounds check the argument map: 1-based indices
	// into the parent arguments tuple, or 0 to take the next constant.
	size8 nConstants = 0;
	for(index8 i = 0; i < childService->nArguments; i++) {
		ASSERT(argumentMap[i] <= nArguments)
		if(!argumentMap[i])
			nConstants++;
	}
	service->impl.permute.nConstants = nConstants;
	if(nConstants) {
		service->impl.permute.constants = Allocate(nConstants * sizeof(Atom));
		CopyMemory(constants, service->impl.permute.constants, nConstants * sizeof(Atom));
		service->impl.permute.constantTypes = Allocate(nConstants);
		CopyMemory(constantTypes, service->impl.permute.constantTypes, nConstants);
		TupleAcquire(constantTypes, constants, nConstants);
	}
	else {
		service->impl.permute.constants = 0;
		service->impl.permute.constantTypes = 0;
	}

	service->impl.permute.argumentMap = Allocate(childService->nArguments);
	CopyMemory(argumentMap, service->impl.permute.argumentMap, childService->nArguments);
	return service;
}


static void permuteServiceSetupContext(ServiceContext * context)
{
	PermuteContext * permuteContext = (PermuteContext *) &context->data;
	size8 nChildArguments = context->service->impl.permute.childService->nArguments;
	
	permuteContext->childArguments = Allocate(nChildArguments * sizeof(Atom));
	for(index8 i = 0, k = 0; i < nChildArguments; i++) {
		int parentIndex = context->service->impl.permute.argumentMap[i];
		Atom a;
		if(parentIndex) {
			// copy parent argument to child, permuted
			a = context->arguments[parentIndex - 1];
		}
		else {
			// copy next constant
			a = context->service->impl.permute.constants[k++];
		}
		permuteContext->childArguments[i] = a;
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
				context->arguments[parentIndex - 1] = permuteContext->childArguments[i];
			}
		}
	}
	return success;
}


static void teardownPermuteService(Service * service)
{
	ASSERT(service->type == SERVICE_PERMUTE)
	ReleaseService(service->impl.permute.childService);
	if(service->impl.permute.nConstants) {
		TupleRelease(
			service->impl.permute.constantTypes,
			service->impl.permute.constants,
			service->impl.permute.nConstants
		);
		Free(service->impl.permute.constants);
		Free(service->impl.permute.constantTypes);
	}
	Free(service->impl.permute.argumentMap);
}


static void permuteServiceFinalizeContext(ServiceContext * context)
{
	PermuteContext * permuteContext = (PermuteContext *) &context->data;
	ServiceFreeContext(permuteContext->childContext);
	Free(permuteContext->childArguments);
}


//------------------------------------- SERVICE_JOIN -----------------------------------------

typedef struct s_JoinContext {
	// Copy of the caller's arguments
	Atom * argumentsCopy;
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
	CopyMemory(joinContext->argumentsCopy, context->arguments, context->service->nArguments * sizeof(Atom));

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
	joinContext->argumentsCopy = Allocate(context->service->nArguments * sizeof(Atom));
	CopyMemory(context->arguments, joinContext->argumentsCopy, context->service->nArguments * sizeof(Atom));
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
	Free(joinContext->argumentsCopy);
}


//-------------------------------------- SERVICE_UNION ------------------------------------------


typedef struct s_UnionContext {
	ServiceContext * lookaheadContext;
	ServiceContext * nextContext;
	Atom * lookahead;
} UnionContext;


Service * CreateUnionService(Service * first, Service * second)
{
	ASSERT(first->nArguments == second->nArguments)
	Service * service = createService(SERVICE_UNION, first->nArguments, sizeof(UnionContext));
	service->impl._union.first = first;
	AcquireService(first);
	service->impl._union.second = second;
	AcquireService(second);
	return service;
}


static void teardownUnionService(Service * service)
{
	ASSERT(service->type == SERVICE_UNION)
	ReleaseService(service->impl._union.first);
	ReleaseService(service->impl._union.second);
}


static void swapContexts(UnionContext * unionContext)
{
	ServiceContext * tmp = unionContext->lookaheadContext;
	unionContext->lookaheadContext = unionContext->nextContext;
	unionContext->nextContext = tmp;
}

static void unionSetupContext(ServiceContext * context)
{
	UnionContext * unionContext = (UnionContext *) &context->data;
	unionContext->lookahead = Allocate(context->service->nArguments * sizeof(Atom));
	// Arbitratily assign child services to previous and next
	// both child services write to the arguments tuple
	unionContext->lookaheadContext = ServiceCreateContext(
		context->service->impl._union.first, context->arguments);
	unionContext->nextContext = ServiceCreateContext(
		context->service->impl._union.second, context->arguments);
	// Obtain the lookahead tuple
	if(ServiceCall(unionContext->lookaheadContext)) {
		CopyMemory(context->arguments, unionContext->lookahead, context->service->nArguments * sizeof(Atom));
	}
	else {
		// No lookahead
		ServiceFreeContext(unionContext->lookaheadContext);
		unionContext->lookaheadContext = 0;
	}	
}


static bool unionServiceCall(ServiceContext * context)
{
	UnionContext * unionContext = (UnionContext *) &context->data;
	size8 nArguments = context->service->nArguments;
	// We interleave tuples provided by the two services to maintain sorted order,
	// and skip any identical tuples. At each call, we have a "lookahead" tuple from
	// one of the child services, and we try to obtain a new tuple from the other
	// service. If one exists, we compare it to the lookahead tuple and return the'
	// preceding one; else we have exhausted one service.
	if(!unionContext->lookaheadContext) {
		// no lookahead service, call other service directly
		return ServiceCall(unionContext->nextContext);
	}
	
	if(!ServiceCall(unionContext->nextContext)) {
		// no more lookahead, swap contexts 
		ServiceFreeContext(unionContext->nextContext);
		unionContext->nextContext = 0;
		swapContexts(unionContext);
		CopyMemory(unionContext->lookahead, context->arguments, nArguments * sizeof(Atom));
		return true;
	}

	// Compare the newly obtained arguments tuple with the lookahead tuple 
	int8 order = TupleCompare(context->arguments, unionContext->lookahead, nArguments);
	if(order == 0) {
		// Duplicate tuple, acquire next (only one tuple can be equal)
		if(ServiceCall(unionContext->nextContext)) {
			order = TupleCompare(context->arguments, unionContext->lookahead, nArguments);
			ASSERT(order > 0)
		}
	}
	if(order < 0) {
		return true;
	}
	else {
		// order > 0
		SwapMemory(context->arguments, unionContext->lookahead, nArguments * sizeof(Atom));
		swapContexts(unionContext);
		return true;
	}
}


static void unionFinalizeContext(ServiceContext * context)
{
	UnionContext * unionContext = (UnionContext *) &context->data;
	if(unionContext->lookaheadContext)
		ServiceFreeContext(unionContext->lookaheadContext);
	ServiceFreeContext(unionContext->nextContext);
	Free(unionContext->lookahead);
}


//------------------------------------- SERVICE_PROJECT -----------------------------------------

/**
 * PROJECT keeps the first nArguments arguments of its child service and drops the rest.
 * Dropping arguments may leave duplicate tuples, so we enumerate the entire child relation
 * into a B-tree keyed on the kept arguments, which both removes duplicates and orders the
 * result. As the kept arguments are a prefix of the child arguments tuple, the B-tree key
 * is simply the leading part of that tuple and needs no rearranging.
 */
typedef struct s_ProjectContext {
	// B-tree holding the unique, ordered tuples
	BTree * btree;
	BTreeIterator iterator;
} ProjectContext;


int8 btreeCompareTuples(void const * item1, void const * item2, size32 itemSize)
{
	return TupleCompare((Atom *) item1, (Atom *) item2, itemSize / sizeof(Atom));
}


static void projectSetupContext(ServiceContext * context)
{
	ProjectContext * projectContext = (ProjectContext *) &context->data;

	Service * childService = context->service->impl.project.childService;
	size8 nArguments = context->service->nArguments;
	size8 nChildArguments = childService->nArguments;

	// The child arguments tuple takes the caller's input arguments in its leading
	// positions; the dropped arguments are left unbound so the child enumerates them.
	Atom * childArguments = Allocate(nChildArguments * sizeof(Atom));
	CopyMemory(context->arguments, childArguments, nArguments * sizeof(Atom));
	SetMemory(&childArguments[nArguments], (nChildArguments - nArguments) * sizeof(Atom), 0);

	ServiceContext * childContext = ServiceCreateContext(childService, childArguments);
	// Retrieve all tuples from the child relation, keeping the leading arguments only
	projectContext->btree = BTreeCreate(
		nArguments * sizeof(Atom),
		btreeCompareTuples,
		0
	);
	while(ServiceCall(childContext)) {
		BTreeInsert(projectContext->btree, childArguments);
	}
	ServiceFreeContext(childContext);
	Free(childArguments);
	// Setup B-tree iterator
	BTreeIterate(&projectContext->iterator, projectContext->btree);
}


static bool projectCall(ServiceContext * context)
{
	ProjectContext * projectContext = (ProjectContext *) &context->data;

	if(BTreeIteratorNext(&projectContext->iterator)) {
		CopyMemory(
			BTreeIteratorPeekItem(&projectContext->iterator),
			context->arguments,
			context->service->nArguments * sizeof(Atom)
		);
		return true;
	}
	else
		return false;
}


static void projectFinalizeContext(ServiceContext * context)
{
	ProjectContext * projectContext = (ProjectContext *) &context->data;
	BTreeIteratorEnd(&projectContext->iterator);
	BTreeFree(projectContext->btree);
}


static void teardownProjectService(Service * service)
{
	ASSERT(service->type == SERVICE_PROJECT)
	ReleaseService(service->impl.project.childService);
}


Service * CreateProjectService(Service * childService, size8 nArguments)
{
	ASSERT(nArguments < childService->nArguments)
	Service * service = createService(SERVICE_PROJECT, nArguments, sizeof(ProjectContext));
	service->impl.project.childService = childService;
	AcquireService(childService);
	return service;
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
	MachineServiceProvider * provider = context->service->impl.machine.provider;
	if(provider->finalizeContext)
		provider->finalizeContext(context);
}


Service * CreateMachineService(size8 nArguments, MachineServiceProvider * provider, void * providerData)
{
	Service * service = createService(SERVICE_MACHINE, nArguments, provider->contextSize);
	service->impl.machine.provider = provider;
	service->impl.machine.providerData = providerData;
	return service;
}


static void tearDownMachineService(Service * service)
{
	MachineServiceProvider * provider = service->impl.machine.provider;
	if(provider->finalizeService)
		provider->finalizeService(service);
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
			teardownUnionService(service);
			break;

		case SERVICE_PROJECT:
			teardownProjectService(service);
			break;

		case SERVICE_MACHINE:
			tearDownMachineService(service);
			break;
		
		default:
			ASSERT(false)
			break;
		}
		Free(service);
	}
}


ServiceContext * ServiceCreateContext(Service const * service, Atom arguments[])
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
		unionSetupContext(context);
		break;

	case SERVICE_PROJECT:
		projectSetupContext(context);
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
		return unionServiceCall(context);

	case SERVICE_PROJECT:
		return projectCall(context);

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
		unionFinalizeContext(context);
		break;

	case SERVICE_PROJECT:
		projectFinalizeContext(context);
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


bool ServiceCallOnce(Service const * service, Atom arguments[])
{
	ServiceContext * context = ServiceCreateContext(service, arguments);
	bool result = ServiceCall(context);
	ServiceFreeContext(context);
	return result;
}


void PrintService(Service const * service)
{
	switch(service->type) {
	case SERVICE_PERMUTE:
		PrintF("PERMUTE/%u(", service->nArguments);
		for(index8 i = 0; i < service->impl.permute.childService->nArguments; i++)
			PrintF("%u ", service->impl.permute.argumentMap[i]);
		PrintChar('{');
		PrintTuple(
			service->impl.permute.constantTypes,
			service->impl.permute.constants,
			service->impl.permute.nConstants
		);
		PrintCString("} ");
		PrintService(service->impl.permute.childService);
		PrintChar(')');
		break;

	case SERVICE_JOIN:
		PrintF("JOIN/%u(", service->nArguments);
		PrintService(service->impl.join.left);
		PrintService(service->impl.join.right);
		PrintChar(')');
		break;

	case SERVICE_UNION:
		PrintF("UNION/%u(", service->nArguments);
		PrintService(service->impl._union.first);
		PrintService(service->impl._union.second);
		PrintChar(')');
		break;

	case SERVICE_PROJECT:
		PrintF("PROJECT/%u(", service->nArguments);
		PrintService(service->impl.project.childService);
		PrintChar(')');
		break;

	case SERVICE_MACHINE:
		PrintF("MACHINE/%u", service->nArguments);
		break;

	default:
		ASSERT(false);
		break;
	}
}
