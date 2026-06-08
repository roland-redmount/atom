
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


/**
 * Compare service record based on the service atom field.
 * Two ServiceRecord compare equal if (1) both forms and parameters match, or
 * (2) forms match and serviceOrKey is 0.
 * 
 * TODO: This does not allow having PARAMETER_IN_OUT subsume other parameters;
 * we now simply compare the parameter lists for equality. So the only cases
 * we can represent is (1) no in/out parameters or (2) all in/out parameters (parameters = 0)
 * For more complex cases, we will need to iterate over matching forms and
 * check for conflicts when adding new services.
 */
static int8 compareServiceRecords(ServiceRecord const * record, ServiceRecord const * recordOrKey)
{
	// extract the 32 bit partial hashes for forms
	if(record->form < recordOrKey->form)
		return -1;
	else if(record->form > recordOrKey->form)
		return 1;
	else {
		// if a search key has no parameters, any record with the same form matches
		if(!recordOrKey->parameters)
			return 0;
		if(record->parameters < recordOrKey->parameters)
			return -1;
		else if(record->parameters  > recordOrKey->parameters)
			return 1;
		else
			return 0;
	}
}


static int8 btreeCompareServiceRecords(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareServiceRecords((ServiceRecord *) item, (ServiceRecord *) itemOrKey);
}


static void btreeFreeService(void * item, size32 itemSize)
{
	ServiceRecord * record = (ServiceRecord *) item;
	// The first 2 core predicate forms are released in RegistryTeardownCoreServices()
	if(record->form > 2)
		IFactRelease(record->form);
	// parameters is set to zero in RegistryTeardownCoreServices()
	if(record->parameters)
		IFactRelease(record->parameters);
}


static index32 findCoreService(Atom form)
{
	for(index32 i = 1; i <= N_CORE_PREDICATES; i++) {
		if(registry.coreServices[i].form == form)
			return i;
	}
	return 0;
}


static void addService(ServiceRecord const * service)
{
	// TODO: here we must ensure that no other service in the registry
	// subsumes or is subsumed by the new service, which would cause
	// conflicts during dispatch
	ASSERT(BTreeInsert(registry.tree, service) == BTREE_INSERTED)
}


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
	Service const * service = &(registry.coreServices[index].service);
	ASSERT(service->type == SERVICE_MACHINE)
	MachineService const * machineService = &(service->value.machineService);
	ASSERT(machineService->provider == &(bTreeServiceProvider))
	return (BTree *) machineService->providerData;
}


void FreeRegistry(void)
{
	ASSERT(BTreeNItems(registry.tree) == 0)
	BTreeFree(registry.tree);
}


TypedAtom btreeParameterGenerator(index32 index, void const * data)
{
	return CreateTypedAtom(AT_PARAMETER, CreateParameter(index + 1, PARAMETER_IN_OUT, AT_NONE));
}


static Atom createBTreeParameterList(size8 arity)
{
	return CreateList(btreeParameterGenerator, 0, arity);
}


void RegistryAddCoreBTreeService(index32 index, Atom form, BTree * btree)
{
	ASSERT(index >= 1);
	ASSERT(index <= N_CORE_PREDICATES)

	ServiceRecord * record = &registry.coreServices[index];
	
	record->form = form;
	MachineService service = {
		.provider = &bTreeServiceProvider,
		.contextSize = sizeof(RelationBTreeIterator),
		.providerData = btree
	};
	size8 arity = RelationBTreeNColumns(btree);
	SetupMachineService(&(record->service), arity, 0, &service);

	// The parameters field will be initialized later
	// by RegistryFinalizeCoreServices() as it requires a list
}


void RegistryFinalizeCoreServices(void)
{
	for(index32 i = 1; i <= N_CORE_PREDICATES; i++) {
		ServiceRecord * record = &(registry.coreServices[i]);
		IFactAcquire(record->form);
		size8 arity = FormArity(record->form);
		record->parameters = createBTreeParameterList(arity);
		// store a copy of the service record in the B-tree
		addService(&(registry.coreServices[i]));
	}
}


void RegistryTeardownCoreServices(void)
{
	// first release and zero out parameter lists from the stored service records
	// to remove the corrsponding tuples from the (list position element) table
	for(index32 i = 1; i <= N_CORE_PREDICATES; i++) {
		ServiceRecord * btreeRecord = BTreePeekItem(registry.tree, &(registry.coreServices[i]));
		IFactRelease(btreeRecord->parameters);
		btreeRecord->parameters = 0;
	}
	// remove core service records, except for (multiset element multiple) and (predicate-form)
	ServiceRecord * record;
	for(index32 i = N_CORE_PREDICATES; i > 2; i--) {
		record = &(registry.coreServices[i]);
		record->parameters = 0;
		ASSERT(BTreeDelete(registry.tree, record) == BTREE_DELETED)

		ASSERT(record->service.type == SERVICE_MACHINE)
 		MachineService * machineService = &(record->service.value.machineService);
		ASSERT(machineService->provider == &bTreeServiceProvider)
		BTree * btree = machineService->providerData;
		ASSERT(RelationBTreeNRows(btree) == 0)
		FreeRelationBTree(btree);
	}
	// Remove (multiset element multiple) and (predicate-form)
	// This must be interleaved since the forms are mutually dependent.
	IFactRelease(GetCorePredicateForm(2));
	IFactRelease(GetCorePredicateForm(1));
	record = &(registry.coreServices[2]);
	record->parameters = 0;
	ASSERT(BTreeDelete(registry.tree, record) == BTREE_DELETED)
	BTree * predicateFormBTree = record->service.value.machineService.providerData;
	ASSERT(RelationBTreeNRows(predicateFormBTree) == 0)
	FreeRelationBTree(predicateFormBTree);

	record = &(registry.coreServices[1]);
	record->parameters = 0;
	ASSERT(BTreeDelete(registry.tree, record) == BTREE_DELETED)
	BTree * multisetBTree = record->service.value.machineService.providerData;
	ASSERT(RelationBTreeNRows(multisetBTree) == 0)
	FreeRelationBTree(multisetBTree);

	SetMemory(registry.coreServices, (N_CORE_PREDICATES + 1) * sizeof(ServiceRecord), 0);
}


void RegistryAddService(ServiceRecord const * record)
{
	addService(record);
	IFactAcquire(record->form);
	IFactAcquire(record->parameters);
}

/**
 * TODO: This is specific to the B-tree service provider, should move to RelationBTree
 */
void RegistryAddBTreeService(Atom form, BTree * btree)
{
	size8 arity = FormArity(form);
	Atom parameters = createBTreeParameterList(arity);
	MachineService btreeService = {
		.provider = &bTreeServiceProvider,
		.contextSize = sizeof(BTreeIterator),
		.providerData = btree
	};
	ServiceRecord record = {
		.form = form,
		.parameters = parameters,
	};
	SetupMachineService(&record.service, arity, 0, &btreeService);
	RegistryAddService(&record);
	IFactRelease(parameters);
}


void RegistryRemoveService(ServiceRecord * record)
{
	ASSERT(BTreeDelete(registry.tree, record) == BTREE_DELETED);
}


static ServiceRecord getServiceRecord(Atom form, Atom parameters)
{
	ServiceRecord record = {
		.form = form,
		.parameters = parameters,
	};
	if(!BTreeGetItem(registry.tree, &record))
		record = (ServiceRecord) {0};
	return record;
}

/**
 * TODO: which services are always untyped? Currently B-Tree,
 * but possibly also "composite" services (JOIN, UNION) ?
 * We might remove this until the architecture is settled ...
 */
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
	Atom parameters = createBTreeParameterList(arity);
	ServiceRecord record = getServiceRecord(form, parameters);
	IFactRelease(parameters);
	// TODO: how to verify the service is untyped? probably need a flag
	return record;
}


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
		if(compareServiceRecords(btreeRecord, &(iterator->keyRecord)) == 0)
			return true;		
	}
	return false;
}


void RegistryIteratorEnd(RegistryIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}


ServiceRecord RegistryFindService(Atom form, Atom parameters)
{
	return getServiceRecord(form, parameters);
}


void PrintServiceRecord(ServiceRecord const * service)
{
	Atom signature = CreateFormula(service->form, service->parameters);
	PrintFormula(signature);
	IFactRelease(signature);
	PrintCString(" => ");
	PrintService(&(service->service));
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

