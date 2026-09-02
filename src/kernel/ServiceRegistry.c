
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
 * B-tree mapping each service-associated operator op to its ancestors, with op as key.
 * An ancestor of an operator depends on that operator.
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
 * Remove a service from the registry.
 */
static void removeService(Service const * service)
{
	if(service->kind == SERVICE_COMPILED)
		nCompiledServices--;

	// Collect all ancestor services of the given service
	ResizingArray ancestorsArray;
	CreateResizingArray(&ancestorsArray, sizeof(Operator *), 10);
	BTreeIterator iterator;
	BTreeIterate(&iterator, operatorAncestors);
	OperatorAncestor key = {.op = service->op};
	if(BTreeIteratorSeek(&iterator, &key)) {
		do {
			OperatorAncestor const * pair = BTreeIteratorPeekItem(&iterator);
			if(pair->op != service->op)
				break;
			ResizingArrayAppend(&ancestorsArray, &(pair->ancestor));
		} while(BTreeIteratorNext(&iterator));
	}
	BTreeIteratorEnd(&iterator);
	// Recursively remove all ancestor services
	Operator ** ancestors = ResizingArrayGetMemory(&ancestorsArray);
	for(index32 i = 0; i < ancestorsArray.nElements; i++) {
		RemoveService(ancestors[i]->relation, ancestors[i]);
	}
	FreeResizingArray(&ancestorsArray);
	// Then remove this service
	DetachOperator(service->op);
	ReleaseRelation(service->relation);
	ASSERT(BTreeDelete(services, service, 0) == BTREE_DELETED)
}

/**
 * Find all operators that are descendants of op in the operator graph
 * and have an associated Relation, and add to the serviceArray.
 * The resulting array may contain duplicates.
 */
void findOperatorDescendants(Operator * op, ResizingArray * serviceArray)
{
	size8 nChildren = OperatorNChildren(op);
	for(index8 i = 0; i < nChildren; i++) {
		findOperatorDescendants(OperatorGetChild(op, i), serviceArray);
	}
	if(op->relation) {
		ResizingArrayAppend(serviceArray, &op);
	}
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
	AttachOperator(op,  relation);

	// Find descendants of the given operator with an attached service
	ResizingArray descendantsArray;
	CreateResizingArray(&descendantsArray, sizeof(Operator *), 10);
	findOperatorDescendants(op, &descendantsArray);
	// Add corresponding records to the ancestor table, except for
	// the self-pair (op, op)
	// NOTE: any duplicates in the array will be rejected by the B-tree
	Operator ** descendants = ResizingArrayGetMemory(&descendantsArray);
	for(index32 i = 0; i < descendantsArray.nElements; i++) {
		if(descendants[i] == op)
			continue;
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
		// whatever was compiled for it is incomplete
		// TODO: this should be moved to CreateRelationTable())
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
	removeService(&service);
}


void ServiceRegistryRemoveAll(Relation const * relation)
{
	// Add all services for the given relation to 
	Service key = {.relation = relation };
	Service service;
	while(BTreeGetItem(services, &key, &service)) {
		removeService(&service);
	}
	// service is shallow-copied by BTreeGetItem() and does not need deallocation
}


void ServiceRegistryInvalidateByTermForm(Atom termForm)
{
	if(nCompiledServices == 0)
		return;

	// Collect "stale" services matching the invalid term form
	ResizingArray staleServices;
	CreateResizingArray(&staleServices, sizeof(Service), 8);
	// Iterate over all relations matching the the termForm
	RelationIterator relationIterator;
	RelationRegistryIterate(termForm, &relationIterator);
	while(RelationIteratorNext(&relationIterator)) {
		Relation const * relation = RelationIteratorGet(&relationIterator);
		// Find all compiled services for this relation
		ServiceIterator serviceIterator;
		ServiceRegistryIterate(relation, &serviceIterator);
		while(ServiceIteratorNext(&serviceIterator)) {
			Service const * service = ServiceIteratorPeekService(&serviceIterator);
			if(service->kind == SERVICE_COMPILED)
				ResizingArrayAppend(&staleServices, service);	// stores a copy
		}
		ServiceIteratorEnd(&serviceIterator);
	}
	RelationIteratorEnd(&relationIterator);

	// remove all stale services
	for(index32 i = 0; i < ResizingArrayNElements(&staleServices); i++) {
		Service * service = ResizingArrayGetElement(&staleServices, i);
		removeService(service);
	}
	FreeResizingArray(&staleServices);
}


void ServiceRegistryInvalidateAll(void)
{
	while(nCompiledServices > 0) {
		// Find the next compiled service
		BTreeIterator iterator;
		BTreeIterate(&iterator, services);
		while(BTreeIteratorNext(&iterator)) {
			Service const * candidate = BTreeIteratorPeekItem(&iterator);
			if(candidate->kind == SERVICE_COMPILED) {
				removeService(candidate);
				// restart from the beginning, cannot iterate while modifying
				break;
			}
		}
		BTreeIteratorEnd(&iterator);
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

