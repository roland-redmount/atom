
#include "datumtypes/Parameter.h"
#include "datumtypes/UInt.h"
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

/*
static int8 compareTypedParameters(TypedAtom typedAtom1, TypedAtom typedAtom2)
{
	return CompareParameters(typedAtom1.atom, typedAtom2.atom);
}
*/

/**
 * The hash value of a ServiceRecord, used for the AT_SERVICE atom value.
 * To generate a B-treee ordering with respect to form and then parameter,
 * use 32 bits of the form atom (an ifact hash) as the upper 32 bits of
 * this hash, and 32 bits from the parameters atom for the lower 32 bits.
 * This allows iterating over all service records for a given form
 * with BTreeIterate() using an ItemComparator that masks out the lower 32
 * bits of the hash (the parameters).
 */
static data64 serviceRecordHash(ServiceRecord const * record)
{
	return ((0xFFFFFFFF & record->form) << 32) | (0xFFFFFFFF & record->parameters);
}


/**
 * Two ServiceRecord compare equal if (1) both forms and parameters match, or
 * (2) forms match and serviceOrKey is 0.
 */
static int8 compareServiceRecords(ServiceRecord const * record, ServiceRecord const * recordOrKey)
{
	return CompareAtoms(record->service, recordOrKey->service);
}


static int8 btreeCompareServiceRecords(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareServiceRecords((ServiceRecord *) item, (ServiceRecord *) itemOrKey);
}


static void btreeFreeService(void * item, size32 itemSize)
{
	ServiceRecord * record = (ServiceRecord *) item;
	// The first 2 core predicate forms are not referenced
	if(record->form > 2)
		IFactRelease(record->form);
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

/*
static TypedAtom btreeParameterGenerator(index32 index, void const * data)
{
	return CreateParameter(PARAMETER_IN_OUT, AT_NONE);
}

static Atom btreeParameterList(size8 arity)
{
	return CreateList(btreeParameterGenerator, 0, arity);
}
*/

BTree * RegistryGetCoreTable(index32 index)
{
	return registry.coreServices[index].provider.tree;
}


void FreeRegistry(void)
{
	// Only the first 2 core services are never removed
	ASSERT(BTreeNItems(registry.tree) == 2)
	BTreeFree(registry.tree);
}


static void addService(ServiceRecord const * service)
{
	// TODO: handle the case of existing service
	ASSERT(BTreeInsert(registry.tree, service) == BTREE_INSERTED)
}


void RegistryAddCoreBTreeService(index32 index, Atom form, size8 arity)
{
	ASSERT(index >= 1);
	ASSERT(index <= N_CORE_PREDICATES)

	ServiceRecord * record = &(registry.coreServices[index]);
	record->type = SERVICE_BTREE;
	record->form = form;
	record->parameters = 0;
	record->service = serviceRecordHash(record);
	record->provider.tree = CreateRelationBTree(arity);
	addService(&(registry.coreServices[index]));
	// While bootstrapping the first 2 core services, we cannot use IFactAcquire()
	if(index > 2)
		IFactAcquire(form);
}


void RegistryRemoveCoreBTreeService(index32 index)
{
	ASSERT(index > 2)
	ServiceRecord * record = &(registry.coreServices[index]);
	ASSERT(BTreeDelete(registry.tree, record) == BTREE_DELETED)
	SetMemory(record, sizeof(ServiceRecord), 0);
}


Atom RegistryAddBTreeService(Atom form, BTree * btree)
{
	ServiceRecord record = {
		.type = SERVICE_BTREE,
		.form = form,
		// For a B-tree service, all parameters are in/out
		.parameters = 0,
		.provider.tree = btree
	};
	record.service = serviceRecordHash(&record);
	addService(&record);
	IFactAcquire(form);
	return (Atom) serviceRecordHash(&record);
}


Atom RegistryAddBytecodeService(Atom signature, Atom bytecode)
{
	ASSERT(IsFormula(signature))
	ServiceRecord record = {
		.form = FormulaGetForm(signature),
		.parameters = FormulaGetActors(signature),
		.type = SERVICE_BYTECODE,
		.provider.bytecode = bytecode
	};
	record.service = serviceRecordHash(&record);
	addService(&record);
	IFactAcquire(record.form);
	IFactAcquire(record.parameters);
	IFactAcquire(record.provider.bytecode);
	return (Atom) serviceRecordHash(&record);
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
	ServiceRecord record = {
		.form = form,
		.parameters = 0	// match any parameter vector
	};
	record.service = serviceRecordHash(&record);
	// TODO: this query might match multiple services ...
	if(!BTreeGetItem(registry.tree, &record))
		record = (ServiceRecord) {0};
	else
		ASSERT(record.type == SERVICE_BTREE)
	return record;
}
