
#include "kernel/Parameter.h"
#include "kernel/UInt.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/lookup.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"
#include "lang/Form.h"
#include "lang/Formula.h"
#include "lang/PredicateForm.h"
#include "memory/pool.h"
#include "util/hashing.h"
#include "vm/bytecode.h"

/**
 * The registry stores all ServiceRecord entries in a BTree, indexed by form.
 * 
 * TODO: this structure must be persisted.
 */
struct {
	// B-tree for service lookup
	BTree * tree;
	// array of core services for fast lookup
	ServiceRecord coreServices[N_CORE_PREDICATES + 1];
} registry;


/**
 * The hash value of a ServiceRecord, used for the AT_SERVICE atom value.
 * To generate a B-tree ordering with respect to form and then parameter,
 * use 32 bits of the form atom (an ifact hash) as the upper 32 bits of
 * this hash, and 32 bits from the parameters atom for the lower 32 bits.
 * This allows iterating over all service records for a given form
 * with BTreeIterate() using an ItemComparator that masks out the lower 32
 * bits of the hash (the parameters).
 */
static data64 serviceRecordHash(Atom form, Atom parameters)
{
	return ((0xFFFFFFFF & form) << 32) | (0xFFFFFFFF & parameters);
}


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
	data32 recordForm32 = record->service >> 32;
	data32 recordOrKeyForm32 = recordOrKey->service >> 32;
	if(recordForm32 < recordOrKeyForm32)
		return -1;
	else if(recordForm32 > recordOrKeyForm32)
		return 1;
	else {
		// extract the 32 bit partial hashes for parameter lists
		data32 recordParameters32 = record->service && 0xFFFFFFFF;
		data32 recordOrKeyParameters32 = recordOrKey->service && 0xFFFFFFFF;
		if(recordParameters32 < recordOrKeyParameters32)
			return -1;
		else if(recordParameters32 > recordOrKeyParameters32)
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
	switch(record->type) {
		case SERVICE_BTREE: {
			size32 nRows = RelationBTreeNRows(record->provider.tree);
			if(nRows > 0) {
				;
				ASSERT(false)
			}
			FreeRelationBTree(record->provider.tree);
			break;
		}
		case SERVICE_BYTECODE:
		IFactRelease(record->provider.bytecode);
		break;

		default:
		break;
	}
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
	// TODO: here we must ensure that no other service exists
	// that can "overlap" with this one during dispatch
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


BTree * RegistryGetCoreTable(index32 index)
{
	return registry.coreServices[index].provider.tree;
}


void FreeRegistry(void)
{
	ASSERT(BTreeNItems(registry.tree) == 0)
	BTreeFree(registry.tree);
}


TypedAtom btreeParameterGenerator(index32 index, void const * data)
{
	return CreateTypedAtom(AT_PARAMETER, CreateParameter(PARAMETER_IN_OUT, AT_NONE));
}


static Atom createBTreeParameterList(size8 arity)
{
	return CreateList(btreeParameterGenerator, 0, arity);
}


BTree * RegistryAddCoreBTreeService(index32 index, Atom form, size8 arity)
{
	ASSERT(index >= 1);
	ASSERT(index <= N_CORE_PREDICATES)

	// store ServiceRecord in the core services array
	ServiceRecord * record = &(registry.coreServices[index]);
	record->type = SERVICE_BTREE;
	record->form = form;
	record->provider.tree = CreateRelationBTree(arity);
	// see RegistryFixCoreServiceParameters() for remaining setup
	return record->provider.tree;
}


void RegistryFinalizeCoreServices(void)
{
	for(index32 i = 1; i <= N_CORE_PREDICATES; i++) {
		ServiceRecord * record = &(registry.coreServices[i]);
		IFactAcquire(record->form);
		size8 arity = FormArity(record->form);
		record->parameters = createBTreeParameterList(arity);
		record->service = serviceRecordHash(record->form, record->parameters);
		// store a copy of the service record in the B-tree
		addService(&(registry.coreServices[i]));
	}
}


void RegistryTeardownCoreServices(void)
{
	// first release and zero out parameter lists from the core service records
	// to remove the corrsponding tuples from the (list position element) table
	for(index32 i = 1; i <= N_CORE_PREDICATES; i++) {
		ServiceRecord * record = BTreePeekItem(registry.tree, &(registry.coreServices[i]));
		IFactRelease(record->parameters);
		record->parameters = 0;
	}
	// remove core service records, except for (multiset element multiple) and (predicate-form)
	for(index32 i = N_CORE_PREDICATES; i > 2; i--) {
		ServiceRecord * record = &(registry.coreServices[i]);
		ASSERT(BTreeDelete(registry.tree, record) == BTREE_DELETED)
	}
	// remove (multiset element multiple) and (predicate-form)
	IFactRelease(GetCorePredicateForm(2));
	IFactRelease(GetCorePredicateForm(1));
	ASSERT(BTreeDelete(registry.tree, &(registry.coreServices[2])) == BTREE_DELETED)
	ASSERT(BTreeDelete(registry.tree, &(registry.coreServices[1])) == BTREE_DELETED)
	SetMemory(registry.coreServices, (N_CORE_PREDICATES + 1) * sizeof(ServiceRecord), 0);
}


Atom RegistryAddBTreeService(Atom form, BTree * btree)
{
	size8 arity = FormArity(form);
	Atom parameters = createBTreeParameterList(arity);
	ServiceRecord record = {
		.service = serviceRecordHash(form, parameters),
		.type = SERVICE_BTREE,
		.form = form,
		.parameters = parameters,
		.provider.tree = btree
	};
	addService(&record);
	IFactAcquire(form);
	// we hold a reference to created parameters already
	return record.service;
}


Atom RegistryAddBytecodeService(Atom form, Atom bytecode)
{
	Atom parameters = BytecodeGetParameters(bytecode);
	ServiceRecord record = {
		.service = serviceRecordHash(form, parameters),
		.form = form,
		.parameters = parameters,
		.type = SERVICE_BYTECODE,
		.provider.bytecode = bytecode
	};
	// TODO: here we must ensure that no other service exists
	// that can "overlap" with this one during dispatch
	addService(&record);
	IFactAcquire(record.form);
	IFactAcquire(record.parameters);
	IFactAcquire(record.provider.bytecode);
	return record.service;
}


void RegistryRemoveService(Atom service)
{
	ServiceRecord record = {
		.service = service
	};
	ASSERT(BTreeDelete(registry.tree, &record) == BTREE_DELETED);
}


ServiceRecord RegistryGetServiceRecord(Atom service)
{
	ServiceRecord record = {
		.service = service,
	};
	if(!BTreeGetItem(registry.tree, &record))
		record = (ServiceRecord) {0};
	return record;
}


ServiceRecord RegistryFindBTreeService(Atom form)
{
	// TODO: this should probably be done via RegistryIterate()

	// First try core tables. This is necessary during bootstrap,
	// before we can use createBTreeParameterList()
	index32 coreServiceIndex = findCoreService(form);
	if(coreServiceIndex)
		return registry.coreServices[coreServiceIndex];

	size8 arity = FormArity(form);
	// NOTE: creating a parameter list here is rather inefficient
	Atom parameters = createBTreeParameterList(arity);
	Atom service = serviceRecordHash(form, parameters);
	ServiceRecord record = RegistryGetServiceRecord(service);
	IFactRelease(parameters);
	ASSERT(record.type == SERVICE_BTREE || record.type == SERVICE_NONE)
	return record;
}


void RegistryIterate(Atom form, RegistryIterator * iterator)
{
	ServiceRecord key = {
		// setting parameters = 0 to match any parameter vector
		// NOTE: here .service is not a valid AT_SERVICE atom
		.service = serviceRecordHash(form, 0)
	};
	BTreeIterate(&(iterator->btreeIterator), registry.tree, &key, 0);
}


bool RegistryIteratorHasService(RegistryIterator const * iterator)
{
	return BTreeIteratorHasItem(&(iterator->btreeIterator));
}


ServiceRecord RegistryIteratorGetService(RegistryIterator * iterator)
{
	ServiceRecord const * btreeRecord = BTreeIteratorPeekItem(&(iterator->btreeIterator));
	// return a copy
	return *btreeRecord;
}


void RegistryIteratorNext(RegistryIterator * iterator)
{
	BTreeIteratorNext(&(iterator->btreeIterator));
}


void RegistryIteratorEnd(RegistryIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}


void PrintService(ServiceRecord const * service)
{
	Atom signature = CreateFormula(service->form, service->parameters);
	PrintFormula(signature);
	IFactRelease(signature);
	PrintChar(' ');
	switch(service->type) {
		case SERVICE_BTREE:
		PrintCString("SERVICE_BTREE");
		break;

		case SERVICE_BYTECODE:
		PrintCString("SERVICE_BYTECODE");
		break;

		default:
		ASSERT(false)
		;
	}
}


static void btreePrintCallback(void const * item)
{
	PrintService((ServiceRecord const *) item);
	PrintChar('\n');
}


void RegistryDump(void)
{
	BTreeTraversal(registry.tree, &btreePrintCallback);
}

