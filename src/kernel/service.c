
#include "btree/btree.h"
#include "kernel/service.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
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
	size8 nArguments, TypedTuple const * constants, index8 const * argumentMap, Service * childService)
{
	Service * service = createService(SERVICE_PERMUTE, nArguments, sizeof(PermuteContext));
	service->impl.permute.childService = childService;
	AcquireService(childService);
	if(constants) {
		service->impl.permute.constants = CreateTupleFromTuple(constants);
		TypedTupleAcquire(service->impl.permute.constants);
	}
	else
		service->impl.permute.constants = 0;
		
#ifdef DEBUG
	// Bounds check the argument map: 1-based indices into the parent arguments tuple,
	// or 0 to take the next constant.
	for(index8 i = 0; i < childService->nArguments; i++)
		ASSERT(argumentMap[i] <= nArguments)
#endif

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
			a = TypedTupleGetAtom(context->service->impl.permute.constants, k++);
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
	if(service->impl.permute.constants) {
		TypedTupleRelease(service->impl.permute.constants);
		FreeTypedTuple(service->impl.permute.constants);
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


//----------------------------------- SERVICE_DEDUPLICATE ---------------------------------------

typedef struct s_DeduplicateContext {
	// B-tree holding the unique, ordered tuples
	BTree * btree;
	BTreeIterator iterator;
} DeduplicateContext;


int8 btreeCompareTuples(void const * item1, void const * item2, size32 itemSize)
{
	return TupleCompare((Atom *) item1, (Atom *) item2, itemSize / sizeof(Atom));
}


static void deduplicateSetupContext(ServiceContext * context)
{
	DeduplicateContext * deduplicateContext = (DeduplicateContext *) &context->data;

	Service * childService = context->service->impl.deduplicate.childService;
	ServiceContext * childContext = ServiceCreateContext(childService, context->arguments);
	// Retrieve all tuples from the child relation
	deduplicateContext->btree = BTreeCreate(
		context->service->nArguments * sizeof(Atom),
		btreeCompareTuples,
		0
	);
	// CreateRelationBTree(context->service->nArguments);
	while(ServiceCall(childContext)) {
		BTreeInsert(deduplicateContext->btree, context->arguments);
	}
	ServiceFreeContext(childContext);
	// Setup B-tree iterator
	BTreeIterate(&deduplicateContext->iterator, deduplicateContext->btree);
}


static bool deduplicateCall(ServiceContext * context)
{
	DeduplicateContext * deduplicateContext = (DeduplicateContext *) &context->data;

	if(BTreeIteratorNext(&deduplicateContext->iterator)) {
		CopyMemory(
			BTreeIteratorPeekItem(&deduplicateContext->iterator),
			context->arguments,
			context->service->nArguments * sizeof(Atom)
		);
		return true;
	}
	else
		return false;
}


static void deduplicateFinalizeContext(ServiceContext * context)
{
	DeduplicateContext * deduplicateContext = (DeduplicateContext *) &context->data;
	BTreeIteratorEnd(&deduplicateContext->iterator);
	BTreeFree(deduplicateContext->btree);
}


static void teardownDeduplicateService(Service * service)
{
	ASSERT(service->type == SERVICE_DEDUPLICATE)
	ReleaseService(service->impl.deduplicate.childService);
}


Service * CreateDeduplicateService(Service * childService)
{
	Service * service = createService(SERVICE_DEDUPLICATE, childService->nArguments, sizeof(DeduplicateContext));
	service->impl.deduplicate.childService = childService;
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
			teardownUnionService(service);
			break;

		case SERVICE_DEDUPLICATE:
			teardownDeduplicateService(service);
			break;

		case SERVICE_MACHINE:
			service->impl.machine.provider->finalizeService(service);
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

	case SERVICE_DEDUPLICATE:
		deduplicateSetupContext(context);
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

	case SERVICE_DEDUPLICATE:
		return deduplicateCall(context);

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

	case SERVICE_DEDUPLICATE:
		deduplicateFinalizeContext(context);
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
		PrintCString("PERMUTE(");
		for(index8 i = 0; i < service->impl.permute.childService->nArguments; i++)
			PrintF("%u ", service->impl.permute.argumentMap[i]);
		TypedTuplePrint(service->impl.permute.constants);
		PrintChar(' ');
		PrintService(service->impl.permute.childService);
		PrintChar(')');
		break;

	case SERVICE_JOIN:
		PrintCString("JOIN(");
		PrintService(service->impl.join.left);
		PrintService(service->impl.join.right);
		PrintChar(')');
		break;

	case SERVICE_UNION:
		PrintCString("UNION(");
		PrintService(service->impl._union.first);
		PrintService(service->impl._union.second);
		PrintChar(')');
		break;

	case SERVICE_DEDUPLICATE:
		PrintCString("DEDUPLICATE(");
		PrintService(service->impl.deduplicate.childService);
		PrintChar(')');
		break;

	case SERVICE_MACHINE:
		PrintCString("MACHINE");
		break;

	default:
		ASSERT(false);
		break;
	}
}
