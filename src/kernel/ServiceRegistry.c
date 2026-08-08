
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "lang/TypedAtom.h"
#include "lang/Formula.h"
#include "memory/allocator.h"


/**
 * The registry of services, as a B-tree storing Service items.
 * The relation tables these refer to are registered separately;
 * see RelationRegistry.h
 */
static BTree * operators;


static void setupService(
	Service * service, RelationTable const * relation, byte const parameterIO[], Operator * op)
{
	service->relation = relation;
	if(parameterIO) {
		service->parameterIO = Allocate(relation->nColumns);
		CopyMemory(parameterIO, service->parameterIO, relation->nColumns);
	}
	else
		service->parameterIO = 0;
	service->op = op;
}


// callback to free the parameterIO allocation upon B-tree deletion of a Service
static void btreeFreeService(void * item, size32 itemSize)
{
	Service * service = item;
	if(service->parameterIO)
		Free(service->parameterIO);
}


static int8 compareServices(Service const * service, Service const * serviceOrKey)
{
	// First compare relations
	if(service->relation < serviceOrKey->relation)
		return -1;
	else if(service->relation > serviceOrKey->relation)
		return 1;
	else {
		// then compare parameter IO lists
		if(!serviceOrKey->parameterIO)
			return 0;
		ASSERT(service->parameterIO)
		return CompareMemory(service->parameterIO, serviceOrKey->parameterIO, service->relation->nColumns);
	}
}


static int8 btreeCompareServices(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareServices((Service *) item, (Service *) itemOrKey);
}


void SetupServiceRegistry(void)
{
	// mapping relations -> services
	operators = BTreeCreate(
		sizeof(Service),
		btreeCompareServices,
		btreeFreeService
	);
}


void FreeServiceRegistry(void)
{
	BTreeFree(operators);
}


Service ServiceRegistryAdd(RelationTable const * relation, byte const parameterIO[], Operator * op)
{
	Service service;
	setupService(&service, relation, parameterIO, op);
	ASSERT(BTreeInsert(operators, &service) == BTREE_INSERTED)
	AcquireOperator(op);
	// NOTE: this returns a copy of the Service struct, but the allocated
	// service.paramaeterIO array is still owned by the registry.
	return service;
}


void ServiceRegistryRemove(RelationTable const * relation, Operator * op)
{
	Service key;
	setupService(&key, relation, 0, op);
	// find the corresponding stored service
	BTreeIterator iterator;
	BTreeIterate(&iterator, operators);
	// NOTE: service stays null unless we find the service, so that we do not
	// mistake the last item we looked at for a match
	Service const * service = 0;
	if(BTreeIteratorSeek(&iterator, &key)) {
		do {
			Service const * candidate = BTreeIteratorPeekItem(&iterator);
			if(candidate->op == op) {
				service = candidate;
				break;
			}
		} while(BTreeIteratorNext(&iterator));
	}
	BTreeIteratorEnd(&iterator);
	ASSERT(service);
	// TODO: this will fail if the B-tree is modified concurrently,
	// invalidating the service pointer.
	ASSERT(BTreeDelete(operators, service, 0) == BTREE_DELETED)
	ReleaseOperator(op);

	btreeFreeService(&key, 0);
}


void ServiceRegistryRemoveAll(RelationTable const * relation)
{
	Service key;
	setupService(&key, relation, 0, 0);
	Service service;
	while(BTreeGetItem(operators, &key, &service)) {
	ASSERT(BTreeDelete(operators, &service, 0) == BTREE_DELETED)
		ReleaseOperator(service.op);
	}
	btreeFreeService(&key, 0);
	// service is shallow-copied by BTreeGetItem() and does not need deallocation
}


size32 ServiceRegistryCount(void)
{
	return BTreeNItems(operators);
}


void ServiceRegistryIterate(RelationTable const * table, ServiceIterator * iterator)
{
	iterator->table = table;
	BTreeIterate(&(iterator->btreeIterator), operators);
}


Service const * ServiceIteratorPeekService(ServiceIterator const * iterator)
{
	return BTreeIteratorPeekItem(&(iterator->btreeIterator));
}


bool ServiceIteratorNext(ServiceIterator * iterator)
{
	Service key = {
		.relation = iterator->table,
		.parameterIO = 0,
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


Operator * ServiceRegistryFind(RelationTable const * relation, byte const parameterIO[])
{
	Service key;
	setupService(&key, relation, parameterIO, 0);

	BTreeIterator iterator;
	BTreeIterate(&iterator, operators);
	Operator * op = 0;
	if(BTreeIteratorSeek(&iterator, &key)) {
		Service * service = BTreeIteratorPeekItem(&iterator);
		op = service->op;
	}
	BTreeIteratorEnd(&iterator);
	btreeFreeService(&key, 0);
	return op;
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
					.atomType =	service->relation->atomTypes[i],
					.io = service->parameterIO[i]
				}
			}
		);
		TypedTupleSetElement(parameters, i, parameter);
	}
	PrintFormActorsAsFormula(service->relation->form, parameters);
	FreeTypedTuple(parameters);
	PrintCString(" => ");
	PrintOperator(service->op);
}


static void btreePrintCallback(void const * item)
{
	PrintService((Service const *) item);
	PrintChar('\n');
}


void RelationTableDump(RelationTable const * table)
{
	// find a service for enumerating all tuples
	byte parameterIO[table->nColumns];
	for(index8 i = 0; i < table->nColumns; i++)
		parameterIO[i] = PARAMETER_OUT;
	Operator const * op = ServiceRegistryFind(table, parameterIO);
	
	PrintF("Table %u columns\n", table->nColumns);

	Atom arguments[table->nColumns];
	OperatorContext * context = OperatorCreateContext(op, arguments);
	size32 nTuples = 0;
	while(OperatorCall(context)) {
		// TODO: we should probably not print the full representaiton
		// of identified atoms, as it triggers repeated queries
		PrintTuple(table->atomTypes, arguments, table->nColumns);
		PrintChar('\n');
		nTuples++;
	}
	OperatorFreeContext(context);
	PrintF("%u tuples\n", nTuples);
}


void ServiceRegistryDump(void)
{
	BTreeTraversal(operators, &btreePrintCallback);
}

