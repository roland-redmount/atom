
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "lang/TypedAtom.h"
#include "lang/formula.h"
#include "memory/allocator.h"
#include "util/ResizingArray.h"


/**
 * The registry of services, as a B-tree storing ServiceRecords.
 * This provides lookup of Services by Relation and IOSignature;
 * see compareServices().
 */
static BTree * serviceRecords;

// Number of registered compiled (non-primitive) services
static size32 nCompiledServices;


/**
 * Order Services by relation, then by IOSignature.
 * A zero IOSignature is a prefix key matching every service of the relation.
 */
static int8 compareServices(Service const * service, Service const * serviceOrKey)
{
	// First compare relations
	int8 relationOrder = CompareRelations(service->relation, serviceOrKey->relation);
	if(relationOrder != 0)
		return relationOrder;
	else {
		// then compare IO signatures; a zeroed signature for the key matches any IO
		if(!serviceOrKey->ioSignature.parameterIO[0])
			return 0;
		return CompareMemory(
			service->ioSignature.parameterIO, serviceOrKey->ioSignature.parameterIO,
			RELATION_MAX_ARITY
		);
	}
}


static int8 btreeCompareServiceRecords(void const * item, void const * itemOrKey, size32 itemSize)
{
	ServiceRecord const * record  = item;
	ServiceRecord const * recordOrKey = itemOrKey;
	return compareServices(&(record->service), &(recordOrKey->service));
}

/**
 * B-tree mapping each service-associated operator <op> to its *immediate* dependents.
 * An immediate dependent is a service of an operator that is an ancestor of <op>, but
 * not an ancestor of any other dependent of <op>.
 * NOTE: this might belong in operator.c
 */
static BTree * operatorAncestors;

typedef struct {
	Operator const * op;
	Operator const * ancestor;
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
	serviceRecords = BTreeCreate(
		sizeof(ServiceRecord),
		btreeCompareServiceRecords,
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
	BTreeFree(serviceRecords);
	BTreeFree(operatorAncestors);
}


/**
 * Copy the ServiceRecord of the given relation evaluated by the given operator to *service.
 * Returns false if the registry holds no such service.
 */
static bool findService(Relation relation, Operator const * op, Service * service)
{
	// Iterate over all services for the given relation
	ServiceIterator iterator;
	ServiceRegistryIterate(relation, &iterator);
	bool found = false;
	while(ServiceIteratorNext(&iterator)) {
		ServiceRecord const * record = ServiceIteratorPeekRecord(&iterator);
		if(record->op == op) {
			*service = record->service;
			found = true;
			break;
		}
	}
	ServiceIteratorEnd(&iterator);
	return found;
}


/**
 * Find the unique operators that are descendants of op in the operator graph
 * and have an associated Relation, and add them to the serviceArray.
 * The search stops at any operator that has an associated Relation,
 * so that we only return the operators for which the service corresponding
 * to operator <op> is an immediate dependent of the returned operator's services.
 */
static void findOperatorDescendants(Operator * op, ResizingArray * serviceArray)
{
	size8 nChildren = OperatorNChildren(op);
	for(index8 i = 0; i < nChildren; i++) {
		Operator * child = OperatorGetChild(op, i);
		if(!IsNullRelation(child->relation)) {
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
void RemoveService(Service service)
{
	// Get a copy of the service record, as changes to the registry
	// may invalidate pointers
	ServiceRecord record;
	ASSERT(BTreeGetItem(serviceRecords, &service, &record));

	if(record.op->type != OPERATOR_MACHINE)
		nCompiledServices--;

	// Find all ancestor services of the given service (dependents)
	// and remove them recursively
	OperatorAncestor key = {.op = record.op};
	OperatorAncestor pair;
	while(BTreeGetItem(operatorAncestors, &key, &pair)) {
		// remove the service identified by the (relation, operator) pair
		Service ancestorService;
		findService(pair.ancestor->relation, pair.ancestor, &ancestorService);
		RemoveService(ancestorService);
	}
	if(OperatorNChildren(record.op) > 0) {
		// Remove any records where this service is the ancestor.
		// This is most efficiently done by following the operator child pointers,
		// as in CreateService(). The descendants themselves are not removed.
		ResizingArray descendantsArray;
		CreateResizingArray(&descendantsArray, sizeof(Operator *), 10);
		findOperatorDescendants(record.op, &descendantsArray);
		Operator ** descendants = ResizingArrayGetMemory(&descendantsArray);
		for(index32 i = 0; i < descendantsArray.nElements; i++) {
			OperatorAncestor pair = {.op = descendants[i], .ancestor = record.op};
			ASSERT(BTreeDelete(operatorAncestors, &pair, 0) == BTREE_DELETED)
		}
		FreeResizingArray(&descendantsArray);
	}
	// Detach the root operator from the service.
	// This may cause the operator to be deleted, and possibly its descendants.
	DetachOperator(record.op);
	ReleaseRelation(service.relation);
	BTreeDeleteResult result = BTreeDelete(serviceRecords, &record, 0);
	ASSERT(result == BTREE_DELETED)
}


void CreateService(Service service, Operator * op)
{
	ServiceRecord record = {
		.service = service,
		.op = op
	};
	AcquireRelation(service.relation);
	AttachOperator(op, service.relation);

	if(op->type == OPERATOR_MACHINE) {
		// Any query of this term form could match this service, so
		// whatever was compiled for such a query it is incomplete.
		// QUESTION: the invalidation scope seems to broad: wouldn't it be enough to invalidate
		// services from the same relation (so that type signature must agree) ?
		InvalidateServicesByTermForm(service.relation.termForm);
	}
	else {
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

		nCompiledServices++;
	}
	// add to the service registry
	ASSERT(BTreeInsert(serviceRecords, &record) == BTREE_INSERTED)
	// return service;
}


// bool ServiceIsPrimitive(Service const * service)
// {
// 	return service->op->type == OPERATOR_MACHINE;
// }


void ServiceRegistryRemoveAll(Relation relation)
{
	// Add all services for the given relation to 
	ServiceRecord key = {.service = (Service) {.relation = relation } };
	ServiceRecord record;
	while(BTreeGetItem(serviceRecords, &key, &record)) {
		RemoveService(record.service);
	}
}


/**
 * Add the immediate ancestor services of the given operator to the given array.
 */
static void collectParentServices(Operator const * op, ResizingArray * ancestorServices)
{
	OperatorAncestor key = {.op = op};
	BTreeIterator iterator;
	BTreeIterate(&iterator, operatorAncestors);
	if(BTreeIteratorSeek(&iterator, &key)) {
		do {
			OperatorAncestor const * pair = BTreeIteratorPeekItem(&iterator);
			if(pair->op != op)
				break;
			Service ancestorService;
			bool found = findService(
				pair->ancestor->relation, pair->ancestor, &ancestorService);
			ASSERT(found)
			ResizingArrayAppend(ancestorServices, &ancestorService);
		} while(BTreeIteratorNext(&iterator));
	}
	BTreeIteratorEnd(&iterator);
}


void InvalidateServicesByTermForm(Atom termForm)
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
		Relation relation = RelationIteratorGet(&relationIterator);
		// Find each compiled service for this relation
		ServiceIterator serviceIterator;
		ServiceRegistryIterate(relation, &serviceIterator);
		while(ServiceIteratorNext(&serviceIterator)) {
			ServiceRecord const * record = ServiceIteratorPeekRecord(&serviceIterator);
			if(record->op->type == OPERATOR_MACHINE) {
				// A primitive service is never stale, but introduction
				// of another service with the same form renders its parents stale,
				// so add them to the list
				collectParentServices(record->op, &staleServices);
			}
			else {
				// For terms invalidated by changes to a rule,
				// A compiled service with the same term form is stale
				ResizingArrayAppend(&staleServices, &(record->service));
			}
		}
		ServiceIteratorEnd(&serviceIterator);
	}
	RelationIteratorEnd(&relationIterator);

	// remove all stale services
	for(index32 i = 0; i < staleServices.nElements; i++) {
		Service * service = ResizingArrayGetElement(&staleServices, i);
		if(!BTreeContainsItem(serviceRecords, service))
			continue;	// service already removed in a previous removeService() call
		RemoveService(*service);
	}
	FreeResizingArray(&staleServices);
}


void RemoveAllCompiledServices(void)
{
	while(nCompiledServices > 0) {
		// Find the next compiled service
		BTreeIterator iterator;
		BTreeIterate(&iterator, serviceRecords);
		Service service;
		bool found = false;
		while(BTreeIteratorNext(&iterator)) {
			ServiceRecord * record = BTreeIteratorPeekItem(&iterator);
			if(record->op->type != OPERATOR_MACHINE) {
				service = record->service;
				found = true;
				break;
			}
		}
		BTreeIteratorEnd(&iterator);
		ASSERT(found)
		RemoveService(service);
		// restart from the beginning, cannot iterate while modifying
	}
}


size32 NumberOfServices(void)
{
	return BTreeNItems(serviceRecords);
}


size32 NumberOfCompiledServices(void)
{
	return nCompiledServices;
}


void ServiceRegistryIterate(Relation relation, ServiceIterator * iterator)
{
	iterator->relation = relation;
	BTreeIterate(&(iterator->btreeIterator), serviceRecords);
}


ServiceRecord const * ServiceIteratorPeekRecord(ServiceIterator const * iterator)
{
	return BTreeIteratorPeekItem(&(iterator->btreeIterator));
}


bool ServiceIteratorNext(ServiceIterator * iterator)
{
	ServiceRecord key = {
		.service = (Service) {
			.relation = iterator->relation,
			.ioSignature = {.parameterIO = {0}}
		},
		.op = 0
	};
	bool foundItem;
	if(BTreeIteratorBeforeFirst(&iterator->btreeIterator)) {
		foundItem = BTreeIteratorSeek(&(iterator->btreeIterator), &key);
	}
	else
		foundItem = BTreeIteratorNext(&(iterator->btreeIterator));

	if(foundItem) {
		ServiceRecord const * record = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(compareServices(&(record->service), &(key.service)) == 0)
			return true;		
	}
	return false;
}


void ServiceIteratorEnd(ServiceIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}


Operator * FindServiceOperator(Service service)
{
	ServiceRecord key = {.service = service};
	ServiceRecord record;
	if(BTreeGetItem(serviceRecords, &key, &record))
		return record.op;
	else
		return 0;
}


void PrintService(Service service)
{
	Operator * op  = FindServiceOperator(service);
	ASSERT(op)

	// Reconstruct a parameter tuple from the IO signature
	// NOTE: could be moved to Parameter.c
	TypedTuple * parameters = CreateTypedTuple(op->nArguments);
	for(index8 i = 0; i < op->nArguments; i++) {
		TypedAtom parameter = CreateTypedAtom(
			AT_PARAMETER,
			(Atom) {
				.parameter = {
					.number = i + 1,
					.atomType =	service.relation.typeSignature.atomTypes[i],
					.io = service.ioSignature.parameterIO[i]
				}
			}
		);
		TypedTupleSetElement(parameters, i, parameter);
	}
	PrintFormActorsAsFormula(service.relation.termForm, parameters);
	FreeTypedTuple(parameters);
	PrintCString(" => ");
	PrintOperator(op);
}


static bool signatureHasInputParameter(IOSignature ioSignature, size8 nParameters)
{
	bool hasInput = false;
	for(index8 i = 0; i < nParameters; i++) {
		if(ioSignature.parameterIO[i] == PARAMETER_IN) {
			hasInput = true;
			break;
		}
	}
	return hasInput;
}

void RelationDump(Relation relation)
{
	// Find an all-outputs service for enumerating all tuples from the relation
	// NOTE: this can be done by dispatch?
	ServiceRecord record = {0};
	ServiceIterator iterator;
	ServiceRegistryIterate(relation, &iterator);
	while(ServiceIteratorNext(&iterator)) {
		ServiceRecord const * candidate = ServiceIteratorPeekRecord(&iterator);
		if(!signatureHasInputParameter(candidate->service.ioSignature, candidate->op->nArguments)) {
			record = *candidate;
			break;
		}
	}
	if(!record.op) {
		PrintCString("Relation cannot be enumerated\n");
		return;
	}

	size8 nArguments = record.op->nArguments;
	PrintF("Relation %u columns\n", nArguments);

	Atom arguments[nArguments];
	OperatorContext * context = OperatorCreateContext(record.op, arguments);
	size32 nTuples = 0;
	while(OperatorCall(context)) {
		// TODO: we should probably not print the full representaiton
		// of identified atoms, as it triggers repeated queries
		PrintTuple(relation.typeSignature.atomTypes, arguments, nArguments);
		PrintChar('\n');
		nTuples++;
	}
	OperatorFreeContext(context);
	PrintF("%u tuples\n", nTuples);
}



static void btreePrintCallback(void const * item)
{
	ServiceRecord const * record = item;
	PrintService(record->service);
	PrintChar('\n');
}


void ServiceRegistryDump(void)
{
	BTreeTraversal(serviceRecords, &btreePrintCallback);
}

