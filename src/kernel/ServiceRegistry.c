
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "lang/TypedAtom.h"
#include "lang/formula.h"
#include "memory/allocator.h"
#include "util/ResizingArray.h"


/**
 * The registry of services, as a B-tree storing Service items.
 * This provides lookup of Services by Relation and IOSignature;
 * see compareServices().
 */
static BTree * services;

// Number of registered services compiled from the rules
static size32 nCompiledServices;


/**
 * Order Service records by relation, then by IOSignature.
 * A zero IOSignature is a prefix key matching every service of the relation.
 */
static int8 compareServices(Service const * service, Service const * serviceOrKey)
{
	// First compare relations
	if(service->relation < serviceOrKey->relation)
		return -1;
	else if(service->relation > serviceOrKey->relation)
		return 1;
	else {
		// then compare IO signatures; a zeroed signature for the key matches any IO
		if(!serviceOrKey->ioSignature.parameterIO[0])
			return 0;
		return CompareMemory(
			service->ioSignature.parameterIO, serviceOrKey->ioSignature.parameterIO,
			service->relation->nColumns);
	}
}


static int8 btreeCompareServices(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareServices((Service *) item, (Service *) itemOrKey);
}

/**
 * B-tree mapping each service-associated operator <op> to its *immediate* dependents.
 * An immediate dependent is a service of an operator that is an ancestor of <op>, but
 * not an ancestor of any other dependent of <op>.
 * NOTE: this might belong in operator.c
 */
static BTree * operatorAncestors;

typedef struct {
	Operator * op;
	Operator * ancestor;
} OperatorAncestor;


static int8 compareOperatorAncestors(OperatorAncestor const * pair, OperatorAncestor const * pairOrKey)
{
	if(pair->op < pairOrKey->op)
		return -1;
	else if(pair->op > pairOrKey->op)
		return 1;
	else if(pairOrKey->ancestor) {
		ASSERT(pair->ancestor)
		if(pair->ancestor < pairOrKey->ancestor)
			return -1;
		else if(pair->ancestor > pairOrKey->ancestor)
			return 1;
	}
	return 0;
}

static int8 btreeCompareOperatorAncestors(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareOperatorAncestors((OperatorAncestor const *) item, (OperatorAncestor const *) itemOrKey);
}


void SetupServiceRegistry(void)
{
	// B-tree of Services, mapping Relation, IOSignature -> Service
	services = BTreeCreate(
		sizeof(Service),
		btreeCompareServices,
		0	// nothing to deallocate
	);

	operatorAncestors = BTreeCreate(
		sizeof(OperatorAncestor),
		btreeCompareOperatorAncestors,
		0	// nothing to deallocate
	);
	nCompiledServices = 0;
}


void FreeServiceRegistry(void)
{
	BTreeFree(services);
	BTreeFree(operatorAncestors);
}


/**
 * Copy the service of the given relation evaluated by the given operator to *service.
 * Returns false if the registry holds no such service.
 */
static bool findService(Relation const * relation, Operator const * op, Service * service)
{
	// Iterate over all services for the given relation
	Service key = {.relation = relation};
	BTreeIterator iterator;
	BTreeIterate(&iterator, services);
	bool found = false;
	if(BTreeIteratorSeek(&iterator, &key)) {
		do {
			Service const * candidate = BTreeIteratorPeekItem(&iterator);
			// the seek finds the first service of the relation, so leave the loop
			// once the services of another relation are reached
			if(compareServices(candidate, &key) != 0)
				break;
			if(candidate->op == op) {
				*service = *candidate;
				found = true;
				break;
			}
		} while(BTreeIteratorNext(&iterator));
	}
	BTreeIteratorEnd(&iterator);
	return found;
}


/**
 * Find the unique operators that are descendants of op in the operator graph
 * and have an associated Relation, and add them to the serviceArray.
 * The search stops at any operator that has an associated Relation,
 * so that we only return the operators for which the service corresponding
 * to op <op> is an immediate dependent of the returned operator's services.
 */
static void findOperatorDescendants(Operator * op, ResizingArray * serviceArray)
{
	size8 nChildren = OperatorNChildren(op);
	for(index8 i = 0; i < nChildren; i++) {
		Operator * child = OperatorGetChild(op, i);
		if(child->relation) {
			// add the service to array, provided it doesn't already exist
			if(!ResizingArrayContainsElement(serviceArray, &child))
				ResizingArrayAppend(serviceArray, &child);
		}
		else
			findOperatorDescendants(child, serviceArray);
	}
}


/**
 * Remove a service from the registry, and remove all OperatorAncestor records
 * where this service is the ancestor.
 */
static void removeService(Service service)
{
	if(service.kind == SERVICE_COMPILED)
		nCompiledServices--;

	// Find all ancestor services of the given service and remove them recursively
	OperatorAncestor key = {.op = service.op};
	OperatorAncestor pair;
	while(BTreeGetItem(operatorAncestors, &key, &pair))
		RemoveService(pair.ancestor->relation, pair.ancestor);

	if(OperatorNChildren(service.op) > 0) {
		// Remove any records where this service is the ancestor.
		// This is most efficiently done by following the operator child pointers,
		// as in CreateService(). The descendants themselves are not removed.
		ResizingArray descendantsArray;
		CreateResizingArray(&descendantsArray, sizeof(Operator *), 10);
		findOperatorDescendants(service.op, &descendantsArray);
		Operator ** descendants = ResizingArrayGetMemory(&descendantsArray);
		for(index32 i = 0; i < descendantsArray.nElements; i++) {
			OperatorAncestor pair = {.op = descendants[i], .ancestor = service.op};
			ASSERT(BTreeDelete(operatorAncestors, &pair, 0) == BTREE_DELETED)
		}
		FreeResizingArray(&descendantsArray);
	}
	DetachOperator(service.op);
	ReleaseRelation(service.relation);
	BTreeDeleteResult result = BTreeDelete(services, &service, 0);
	ASSERT(result == BTREE_DELETED)
}


Service CreateService(
	Relation const * relation, IOSignature ioSignature, Operator * op,
	enum ServiceKind kind)
{
	ASSERT(relation->nColumns <= RELATION_MAX_ARITY)
	Service service = {
		.relation = relation,
		.kind = kind,
		.ioSignature = ioSignature,
		.op = op
	};
	AcquireRelation(relation);
	AttachOperator(op, relation);

	// Find descendants of the given operator with an attached service.
	// The given service is a dependent of these operators' services.
	ResizingArray descendantsArray;
	CreateResizingArray(&descendantsArray, sizeof(Operator *), 10);
	findOperatorDescendants(op, &descendantsArray);
	// Add corresponding records to the ancestor table.
	// NOTE: any duplicates in the array will be rejected by the B-tree
	Operator ** descendants = ResizingArrayGetMemory(&descendantsArray);
	for(index32 i = 0; i < descendantsArray.nElements; i++) {
		ASSERT(descendants[i] != op)
		OperatorAncestor pair = {.op = descendants[i], .ancestor = op};
		BTreeInsert(operatorAncestors, &pair);
	}
	FreeResizingArray(&descendantsArray);

	// add to the service registry
	ASSERT(BTreeInsert(services, &service) == BTREE_INSERTED)

	switch(kind) {
	case SERVICE_COMPILED:
		nCompiledServices++;
		break;

	case SERVICE_PRIMITIVE:
		// A query of this term form may now have one more relation to match, so
		// whatever was compiled for it is incomplete.
		// QUESTION: the invalidation scope seems to broad: wouldn't it be enough to invalidate
		// services from the same relation (so that type signature must agree) ?
		ServiceRegistryInvalidateByTermForm(relation->termForm);
		break;
	}
	return service;
}


bool ServiceHasDependents(Service const * service)
{
	// A service is a dependent if its operator is an ancestor
	// of the given service's operator.
	OperatorAncestor key = {.op = service->op};
	return BTreeContainsItem(operatorAncestors, &key);
}


void RemoveService(Relation const * relation, Operator * op)
{
	Service service;
	bool found = findService(relation, op, &service);
	ASSERT(found)
	removeService(service);
}


void ServiceRegistryRemoveAll(Relation const * relation)
{
	// Add all services for the given relation to 
	Service key = {.relation = relation };
	Service service;
	while(BTreeGetItem(services, &key, &service)) {
		removeService(service);
	}
	// service is shallow-copied by BTreeGetItem() and does not need deallocation
}


/**
 * Add the immediate ancestor services of the given service to the given array.
 */
static void collectParentServices(Service const * service, ResizingArray * ancestorServices)
{
	OperatorAncestor key = {.op = service->op};
	BTreeIterator iterator;
	BTreeIterate(&iterator, operatorAncestors);
	if(BTreeIteratorSeek(&iterator, &key)) {
		do {
			OperatorAncestor const * pair = BTreeIteratorPeekItem(&iterator);
			if(pair->op != service->op)
				break;
			Service ancestorService;
			bool found = findService(
				pair->ancestor->relation, pair->ancestor, &ancestorService);
			ASSERT(found)
			ASSERT(ancestorService.kind == SERVICE_COMPILED)
			ResizingArrayAppend(ancestorServices, &ancestorService);
		} while(BTreeIteratorNext(&iterator));
	}
	BTreeIteratorEnd(&iterator);
}


void ServiceRegistryInvalidateByTermForm(Atom termForm)
{
	if(nCompiledServices == 0)
		return;

	// Collect "stale" services matching the term form
	ResizingArray staleServices;
	CreateResizingArray(&staleServices, sizeof(Service), 8);
	// Iterate over all relations matching the the termForm
	RelationIterator relationIterator;
	RelationRegistryIterate(termForm, &relationIterator);
	while(RelationIteratorNext(&relationIterator)) {
		Relation const * relation = RelationIteratorGet(&relationIterator);
		// Find each compiled service for this relation
		ServiceIterator serviceIterator;
		ServiceRegistryIterate(relation, &serviceIterator);
		while(ServiceIteratorNext(&serviceIterator)) {
			Service const * service = ServiceIteratorPeekService(&serviceIterator);
			switch(service->kind) {
			case SERVICE_COMPILED:
				// For terms invalidated by changes to a rule,
				// A compiled service with the same term form is stale
				ResizingArrayAppend(&staleServices, service);
				break;

			case SERVICE_PRIMITIVE:
				// A PRIMITIVE service is never stale, but introduction
				// of another service with the same form renders its parents stale,
				// so add them to the list
				collectParentServices(service, &staleServices);
				break;
			}
		}
		ServiceIteratorEnd(&serviceIterator);
	}
	RelationIteratorEnd(&relationIterator);

	// remove all stale services
	for(index32 i = 0; i < ResizingArrayNElements(&staleServices); i++) {
		Service service = * ((Service *) ResizingArrayGetElement(&staleServices, i));
		if(!BTreeContainsItem(services, &service))
			continue;	// service already removed in a previous removeService() call
		removeService(service);
	}
	FreeResizingArray(&staleServices);
}


void RemoveAllCompiledServices(void)
{
	while(nCompiledServices > 0) {
		// Find the next compiled service
		BTreeIterator iterator;
		BTreeIterate(&iterator, services);
		Service service = {0};
		while(BTreeIteratorNext(&iterator)) {
			Service * candidate = BTreeIteratorPeekItem(&iterator);
			if(candidate->kind == SERVICE_COMPILED) {
				service = *candidate;
				break;
			}
		}
		BTreeIteratorEnd(&iterator);
		if(service.relation)
			removeService(service);
		// restart from the beginning, cannot iterate while modifying
	}
}


size32 ServiceRegistryCount(void)
{
	return BTreeNItems(services);
}


size32 ServiceRegistryNCompiled(void)
{
	return nCompiledServices;
}


void ServiceRegistryIterate(Relation const * relation, ServiceIterator * iterator)
{
	iterator->relation = relation;
	BTreeIterate(&(iterator->btreeIterator), services);
}


Service const * ServiceIteratorPeekService(ServiceIterator const * iterator)
{
	return BTreeIteratorPeekItem(&(iterator->btreeIterator));
}


bool ServiceIteratorNext(ServiceIterator * iterator)
{
	Service key = {
		.relation = iterator->relation,
		.ioSignature = {.parameterIO = {0}},
		.op = 0
	};
	bool foundItem;
	if(BTreeIteratorBeforeFirst(&iterator->btreeIterator)) {
		foundItem = BTreeIteratorSeek(&(iterator->btreeIterator), &key);
	}
	else
		foundItem = BTreeIteratorNext(&(iterator->btreeIterator));

	if(foundItem) {
		Service const * btreeService = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(compareServices(btreeService, &key) == 0)
			return true;		
	}
	return false;
}


void ServiceIteratorEnd(ServiceIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}


Operator * ServiceRegistryFind(Relation const * relation, IOSignature ioSignature)
{
	Service key = {.relation = relation, .ioSignature = ioSignature};
	// QUESTION: Why use an iterator here to seek to a single item?
	BTreeIterator iterator;
	BTreeIterate(&iterator, services);
	Operator * op = 0;
	if(BTreeIteratorSeek(&iterator, &key)) {
		Service * service = BTreeIteratorPeekItem(&iterator);
		op = service->op;
	}
	BTreeIteratorEnd(&iterator);
	return op;
}


bool ServiceRegistryFindByMachineProvider(
	MachineProvider const * provider, Service * service)
{
	bool found = false;
	// NOTE: this is a full table scan
	BTreeIterator iterator;
	BTreeIterate(&iterator, services);
	while(!found && BTreeIteratorNext(&iterator)) {
		Service const * candidate = BTreeIteratorPeekItem(&iterator);
		if((candidate->op->type == OPERATOR_MACHINE)
			&& (candidate->op->impl.machine.provider == provider)) {
			*service = *candidate;
			found = true;
		}
	}
	BTreeIteratorEnd(&iterator);
	return found;
}


void PrintService(Service const * service)
{
	TypedTuple * parameters = CreateTypedTuple(service->op->nArguments);
	for(index8 i = 0; i < service->op->nArguments; i++) {
		TypedAtom parameter = CreateTypedAtom(
			AT_PARAMETER,
			(Atom) {
				.parameter = {
					.number = i + 1,
					.atomType =	service->relation->typeSignature.atomTypes[i],
					.io = service->ioSignature.parameterIO[i]
				}
			}
		);
		TypedTupleSetElement(parameters, i, parameter);
	}
	PrintFormActorsAsFormula(service->relation->termForm, parameters);
	FreeTypedTuple(parameters);
	PrintCString(" => ");
	PrintOperator(service->op);
}


static void btreePrintCallback(void const * item)
{
	PrintService((Service const *) item);
	PrintChar('\n');
}


void RelationDump(Relation const * relation)
{
	// find a service for enumerating all tuples
	byte parameterIO[relation->nColumns];
	for(index8 i = 0; i < relation->nColumns; i++)
		parameterIO[i] = PARAMETER_OUT;
	Operator const * op = ServiceRegistryFind(
		relation, CreateIOSignature(parameterIO, relation->nColumns));
	ASSERT(op);

	PrintF("Relation %u columns\n", relation->nColumns);

	Atom arguments[relation->nColumns];
	OperatorContext * context = OperatorCreateContext(op, arguments);
	size32 nTuples = 0;
	while(OperatorCall(context)) {
		// TODO: we should probably not print the full representaiton
		// of identified atoms, as it triggers repeated queries
		PrintTuple(relation->typeSignature.atomTypes, arguments, relation->nColumns);
		PrintChar('\n');
		nTuples++;
	}
	OperatorFreeContext(context);
	PrintF("%u tuples\n", nTuples);
}


void ServiceRegistryDump(void)
{
	BTreeTraversal(services, &btreePrintCallback);
}

