
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "lang/TypedAtom.h"
#include "lang/Formula.h"
#include "memory/allocator.h"


/**
 * The registry of services, as a B-tree storing Service items.
 * The relation tables these records refer to are registered separately;
 * see RelationRegistry.h
 */
static BTree * operators;


static void setupService(
	Service * record, RelationTable const * relation, byte const parameterIO[], Operator * op)
{
	record->relation = relation;
	if(parameterIO) {
		record->parameterIO = Allocate(relation->nColumns);
		CopyMemory(parameterIO, record->parameterIO, relation->nColumns);
	}
	else
		record->parameterIO = 0;
	record->op = op;
}


// callback to free the parameterIO allocation upon B-tree deletion of a Service
static void btreeFreeService(void * item, size32 itemSize)
{
	Service * record = item;
	if(record->parameterIO)
		Free(record->parameterIO);
}


static int8 compareServices(Service const * record, Service const * recordOrKey)
{
	// First compare relations
	if(record->relation < recordOrKey->relation)
		return -1;
	else if(record->relation > recordOrKey->relation)
		return 1;
	else {
		// then compare parameter IO lists
		if(!recordOrKey->parameterIO)
			return 0;
		ASSERT(record->parameterIO)
		return CompareMemory(record->parameterIO, recordOrKey->parameterIO, record->relation->nColumns);
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
	Service record;
	setupService(&record, relation, parameterIO, op);
	ASSERT(BTreeInsert(operators, &record) == BTREE_INSERTED)
	AcquireOperator(op);
	return record;
}


void ServiceRegistryRemove(RelationTable const * relation, Operator * op)
{
	Service key;
	setupService(&key, relation, 0, op);
	// find the corresponding service record
	BTreeIterator iterator;
	BTreeIterate(&iterator, operators);
	// NOTE: record stays null unless we find the service, so that we do not
	// mistake the last record we looked at for a match
	Service const * record = 0;
	if(BTreeIteratorSeek(&iterator, &key)) {
		do {
			Service const * candidate = BTreeIteratorPeekItem(&iterator);
			if(candidate->op == op) {
				record = candidate;
				break;
			}
		} while(BTreeIteratorNext(&iterator));
	}
	BTreeIteratorEnd(&iterator);
	ASSERT(record);
	// TODO: this will fail if the B-tree is modified concurrently,
	// invalidating the record pointer.
	ASSERT(BTreeDelete(operators, record, 0) == BTREE_DELETED)
	ReleaseOperator(op);

	btreeFreeService(&key, 0);
}


void ServiceRegistryRemoveAll(RelationTable const * relation)
{
	Service key;
	setupService(&key, relation, 0, 0);
	Service record;
	while(BTreeGetItem(operators, &key, &record)) {
	ASSERT(BTreeDelete(operators, &record, 0) == BTREE_DELETED)
		ReleaseOperator(record.op);
	}
	btreeFreeService(&key, 0);
	// record is shallow-copied by BTreeGetItem() and does not need deallocation
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
		Service const * btreeRecord = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(compareServices(btreeRecord, &key) == 0)
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
		Service * record = BTreeIteratorPeekItem(&iterator);
		op = record->op;
	}
	BTreeIteratorEnd(&iterator);
	btreeFreeService(&key, 0);
	return op;
}


void PrintService(Service const * record)
{
	TypedTuple * parameters = CreateTypedTuple(record->op->nArguments);
	for(index8 i = 0; i < record->op->nArguments; i++) {
		TypedAtom parameter = CreateTypedAtom(
			AT_PARAMETER,
			(Atom) {
				.parameter = {
					.number = i + 1,
					.atomType =	record->relation->atomTypes[i],
					.io = record->parameterIO[i]
				}
			}
		);
		TypedTupleSetElement(parameters, i, parameter);
	}
	PrintFormActorsAsFormula(record->relation->form, parameters);
	FreeTypedTuple(parameters);
	PrintCString(" => ");
	PrintOperator(record->op);
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

