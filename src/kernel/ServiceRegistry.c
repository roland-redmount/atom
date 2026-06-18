
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/lookup.h"
#include "kernel/Parameter.h"
#include "kernel/RelationBTree.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/UInt.h"
#include "lang/TypedAtom.h"
#include "lang/Form.h"
#include "lang/Formula.h"
#include "lang/PredicateForm.h"
#include "util/hashing.h"


/**
 * We store all ServiceRecord entries in a BTree. Pointers to
 * service records are not stable except when peeking from iterators.
 */
struct {
	// B-tree for lookup
	BTree * tree;
	// copy of core service records for fast lookup
	ServiceRecord coreServices[N_CORE_PREDICATES + 1];
} registry;


int8 CompareServiceRecords(ServiceRecord const * record, ServiceRecord const * recordOrKey)
{
	if(record->form.hash < recordOrKey->form.hash)
		return -1;
	else if(record->form.hash > recordOrKey->form.hash)
		return 1;
	else {
		// if a search key has no parameters, any record with the same form matches
		if(recordOrKey->parameters)
			return CompareMemory(record->parameters, recordOrKey->parameters, record->service->nArguments);
		else
			return 0;
	}
}


static int8 btreeCompareServiceRecords(void const * item, void const * itemOrKey, size32 itemSize)
{
	return CompareServiceRecords((ServiceRecord *) item, (ServiceRecord *) itemOrKey);
}


static void btreeFreeService(void * item, size32 itemSize)
{
	ServiceRecord * record = (ServiceRecord *) item;
	// The first 2 core predicate forms are released in RegistryTeardownCoreServices()
	if(record->form.hash > 2)
		IFactRelease(record->form);
	// parameters is set to zero in RegistryTeardownCoreServices()
	if(record->parameters)
		Free(record->parameters);
	ReleaseService(record->service);
}


static index32 findCoreService(Atom form)
{
	for(index32 i = 1; i <= N_CORE_PREDICATES; i++) {
		if(registry.coreServices[i].form.hash == form.hash)
			return i;
	}
	return 0;
}

/*
static void addService(ServiceRecord const * service)
{
	// This will shallow-copy the ServiceRecord
	ASSERT(BTreeInsert(registry.tree, service) == BTREE_INSERTED)
	AcquireService(service->service);
}
*/

void SetupRegistry(void)
{
	registry.tree = BTreeCreate(
	    sizeof(ServiceRecord),
	    btreeCompareServiceRecords,
	    btreeFreeService
	);
	SetMemory(registry.coreServices, (N_CORE_PREDICATES + 1) * sizeof(ServiceRecord), 0);
}


size32 RegistryNServices(void)
{
	return BTreeNItems(registry.tree);
}


ServiceRecord const * RegistryGetCoreServiceRecord(index32 index)
{
	return &registry.coreServices[index];
}


BTree * RegistryGetCoreBTreeService(index32 index)
{
	Service const * service = registry.coreServices[index].service;
	ASSERT(service->type == SERVICE_MACHINE)
	ASSERT(service->impl.machine.provider == &(bTreeServiceProvider))
	return (BTree *) service->impl.machine.providerData;
}


void FreeRegistry(void)
{
	ASSERT(BTreeNItems(registry.tree) == 0)
	BTreeFree(registry.tree);
}


TypedAtom btreeParameterGenerator(index32 index, void const * data)
{
	Atom parameter = (Atom) {
		.parameter = {.number = index + 1, .io = PARAMETER_IN_OUT, .atomType = AT_NONE}
	};
	return CreateTypedAtom(AT_PARAMETER, parameter);
}


static void setupBTreeParameters(RelationBTree * tree, Atom * parameters)
{
	for(index8 i = 0; i < tree->nColumns; i++) {
		parameters[i] = (Atom) {
				.parameter = {
				.number = i + 1,
				.io = PARAMETER_IN_OUT,
				.atomType = tree->atomTypes[i]
			}
		};
	}
}


void RegistryAddCoreBTreeService(index32 index, Atom form, BTree * btree)
{
	ASSERT(index >= 1);
	ASSERT(index <= N_CORE_PREDICATES)

	ServiceRecord * record = &registry.coreServices[index];

	record->form = form;
	size8 arity = RelationBTreeNColumns(btree);
	record->service = CreateMachineService(arity, &bTreeServiceProvider, btree);

	// The record->parameters field will be initialized later
	// by RegistryFinalizeCoreServices()
	// TODO: is this still necessary when parameters is an array?
}


void RegistryFinalizeCoreServices(void)
{
	for(index32 i = 1; i <= N_CORE_PREDICATES; i++) {
		ServiceRecord * record = &(registry.coreServices[i]);
		IFactAcquire(record->form);
		size8 arity = record->service->nArguments;
		record->parameters = Allocate(arity);
		RelationBTree * tree = record->service->impl.machine.providerData;
		for(index8 i = 0; i < arity; i++) {
			record->parameters[i] = (Atom) {
				.parameter = {
					.number = i + 1,
					.io = PARAMETER_IN_OUT,
					.atomType = tree->atomTypes[i]
				}
			};
		}
		// store a copy of the service record in the B-tree
		addService(record);
		ReleaseService(record->service);
	}
}


void RegistryTeardownCoreServices(void)
{
	// first release and zero out parameter lists from the stored service records
	// to remove the corrsponding tuples from the (list position element) table
	for(index32 i = 1; i <= N_CORE_PREDICATES; i++) {
		ServiceRecord * btreeRecord = BTreePeekItem(registry.tree, &(registry.coreServices[i]));
		Free(btreeRecord->parameters);
		btreeRecord->parameters = 0;
	}
	// remove core service records, except for (multiset element multiple) and (predicate-form)
	ServiceRecord * record;
	for(index32 i = N_CORE_PREDICATES; i > 2; i--) {
		record = &(registry.coreServices[i]);
		record->parameters = 0;
		ASSERT(record->service->type == SERVICE_MACHINE)
 		ASSERT(record->service->impl.machine.provider == &bTreeServiceProvider)
		BTree * btree = record->service->impl.machine.providerData;
		ASSERT(RelationBTreeNRows(btree) == 0)
		FreeRelationBTree(btree);
		ASSERT(BTreeDelete(registry.tree, record) == BTREE_DELETED)
	}
	// Remove (multiset element multiple) and (predicate-form)
	// This must be interleaved since the forms are mutually dependent.
	IFactRelease(GetCorePredicateForm(2));
	IFactRelease(GetCorePredicateForm(1));
	record = &(registry.coreServices[2]);
	record->parameters = 0;
	ASSERT(BTreeDelete(registry.tree, record) == BTREE_DELETED)
	BTree * predicateFormBTree = record->service->impl.machine.providerData;
	ASSERT(RelationBTreeNRows(predicateFormBTree) == 0)
	FreeRelationBTree(predicateFormBTree);

	record = &(registry.coreServices[1]);
	record->parameters = 0;
	ASSERT(BTreeDelete(registry.tree, record) == BTREE_DELETED)
	BTree * multisetBTree = record->service->impl.machine.providerData;
	ASSERT(RelationBTreeNRows(multisetBTree) == 0)
	FreeRelationBTree(multisetBTree);

	SetMemory(registry.coreServices, (N_CORE_PREDICATES + 1) * sizeof(ServiceRecord), 0);
}


void RegistryAddService(Atom predicateForm, Atom const * parameters, Service const * service)
{
	size8 arity = PredicateArity(predicateForm);
	ASSERT(arity == service->nArguments);
	ServiceRecord record;
	record.form = predicateForm;
	record.parameters = Allocate(arity);
	CopyMemory(parameters, record.parameters, arity);
	record.service = service;
	// this will shallow-copy the ServiceRecord
	ASSERT(BTreeInsert(registry.tree, &record) == BTREE_INSERTED)
	IFactAcquire(predicateForm);
	AcquireService(service);
}

/**
 * TODO: This is specific to the B-tree service provider, should move to RelationBTree
 */
void RegistryAddBTreeService(Atom form, RelationBTree * tree)
{
	Atom parameters[tree->nColumns];
	setupBTreeParameters(tree, parameters);
	Service * service = CreateMachineService(tree->nColumns, &bTreeServiceProvider, tree);
	return RegistryAddService(form, parameters, service);
	ReleaseService(service);
}


void RegistryRemoveService(ServiceRecord * record)
{
	ASSERT(BTreeDelete(registry.tree, record) == BTREE_DELETED);
}


static ServiceRecord getServiceRecord(Atom form, Atom const * parameters)
{
	// return a copy of the service record since its address is not stable
	ServiceRecord record = {
		.form = form,
		.parameters = parameters,
	};
	if(!BTreeGetItem(registry.tree, &record))
		record = (ServiceRecord) {0};
	return record;
}


/*
ServiceRecord RegistryFindUntypedService(Atom form)
{
	// TODO: this should probably be done via RegistryIterate()

	// First try core tables. This is necessary during bootstrap,
	// before we can use createBTreeParameterList()
	// NOTE: this should perhaps be a dedicated bootstrap function
	index32 coreServiceIndex = findCoreService(form);
	if(coreServiceIndex)
		return registry.coreServices[coreServiceIndex];

	size8 arity = FormArity(form);
	// NOTE: creating a parameter list here is rather inefficient
	Atom parameters = setupBTreeParameters(arity);
	ServiceRecord record = getServiceRecord(form, parameters);
	IFactRelease(parameters);
	// TODO: how to verify the service is untyped? probably need a flag
	return record;
}
*/

void RegistryIterate(Atom form, RegistryIterator * iterator)
{
	iterator->keyRecord = (ServiceRecord) {
		.form = form,
		// setting parameters = 0 to match any parameter vector
		.parameters = 0,
	};
	BTreeIterate(&(iterator->btreeIterator), registry.tree);
}


ServiceRecord const * RegistryIteratorPeekService(RegistryIterator * iterator)
{
	return BTreeIteratorPeekItem(&(iterator->btreeIterator));
}


bool RegistryIteratorNext(RegistryIterator * iterator)
{
	bool foundItem;
	if(BTreeIteratorBeforeFirst(&iterator->btreeIterator))
		foundItem = BTreeIteratorSeek(&(iterator->btreeIterator), &(iterator->keyRecord));
	else
		foundItem = BTreeIteratorNext(&(iterator->btreeIterator));

	if(foundItem) {
		ServiceRecord const * btreeRecord = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(CompareServiceRecords(btreeRecord, &(iterator->keyRecord)) == 0)
			return true;		
	}
	return false;
}


void RegistryIteratorEnd(RegistryIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}


ServiceRecord RegistryFindService(Atom form, Atom const * parameters)
{
	return getServiceRecord(form, parameters);
}


void PrintServiceRecord(ServiceRecord const * record)
{
	TypedTuple * parameters = CreateTypedTyple(record->service->nArguments);
	for(index8 i = 0; i < record->service->nArguments; i++) {
		TypedTupleSetElement(parameters, i, 
			CreateTypedAtom(AT_PARAMETER, record->parameters[i]));
	}
	PrintFormActorsAsFormula(service->form, parameters);
	FreeTuple(parameters);
	PrintCString(" => ");
	PrintService(record->service);
}


static void btreePrintCallback(void const * item)
{
	PrintServiceRecord((ServiceRecord const *) item);
	PrintChar('\n');
}


void RegistryDump(void)
{
	BTreeTraversal(registry.tree, &btreePrintCallback);
}

