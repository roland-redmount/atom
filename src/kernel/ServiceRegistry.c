
#include "datumtypes/UInt.h"
#include "kernel/kernel.h"
#include "kernel/lookup.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Atom.h"
#include "lang/Form.h"
#include "lang/Formula.h"
#include "memory/pool.h"
#include "vm/bytecode.h"

/**
 * The registry stores all Service entries in a BTree, indexed by form.
 * 
 * TODO: this structure must be persisted.
 */
struct {
	// B-tree for service lookup
	BTree * tree;
	BTree * coreTables[N_CORE_PREDICATES + 1];
} registry;


/**
 * For searching
 */

 /*
static int8 compareServicesByForm(Service const * service1, Service const * service2)
{
	return CompareAtoms(service1->form, service2->form);
}

static int btreeCompareServicesByForm(void const * item1, void const * item2, void * udata)
{
	return compareServicesByForm((Service *) item1, (Service *) item2);
}
*/


/**
 * Services are ordered by form, then by type, then by parameters.
 */
static int8 compareServices(Service const * service, Service const * serviceOrKey)
{
	int8 formOrder = CompareAtoms(service->form, serviceOrKey->form);
	if(formOrder == 0) {
		if(service->type < serviceOrKey->type)
			return -1;
		else if(service->type > serviceOrKey->type)
			return 1;
		else {
			return CompareAtoms(service->parameters, serviceOrKey->parameters);
		}
	}
	else
		return formOrder;
}

static int8 btreeCompareServices(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareServices((Service *) item, (Service *) itemOrKey);
}



static void btreeFreeService(void * item, size32 itemSize)
{
	Service * service = (Service *) item;
	switch(service->type) {
		case SERVICE_BTREE:
		ASSERT(RelationBTreeNRows(service->service.tree) == 0)
		FreeRelationBTree(service->service.tree);
		break;
		
		case SERVICE_BYTECODE:
		ReleaseAtom(service->service.bytecode);
		break;

		default:
		break;
	}
}


void SetupRegistry(void)
{
	registry.tree = BTreeCreate(
	    sizeof(Service),
	    btreeCompareServices,		// compare_items
	    btreeFreeService
	);
	SetMemory(registry.coreTables, (N_CORE_PREDICATES + 1) * sizeof(BTree *), 0);
}


size32 RegistryNServices(void)
{
	return BTreeNItems(registry.tree);
}


// create a Service struct
static Service createBTreeService(Atom form, BTree * btree)
{
	Service service;
	service.form = form;
	service.parameters = invalidAtom;	// not used for relation tables
	service.type = SERVICE_BTREE;
	service.service.tree = btree;
	return service;
}


void RegistryCreateCoreTable(index32 index, Atom form, size8 arity)
{
	ASSERT(index >= 1);
	ASSERT(index <= N_CORE_PREDICATES)

	Service service = createBTreeService(form, CreateRelationBTree(arity));
	ASSERT(BTreeInsert(registry.tree, &service) == BTREE_INSERTED)

	registry.coreTables[index] = service.service.tree;
}


BTree * RegistryGetCoreTable(index32 index)
{
	return registry.coreTables[index];
}


void TeardownRegistry(void)
{
	BTreeFree(registry.tree);
}


BTree * RegistryCreateTable(Atom form)
{
	Service service = createBTreeService(form, CreateRelationBTree(FormArity(form)));
	if(BTreeInsert(registry.tree, &service) == BTREE_INSERTED)
		return service.service.tree;
	else
		return 0;
}


BTree * RegistryLookupTable(Atom form)
{
	Service service = createBTreeService(form, 0);
	bool found = BTreeGetItem(registry.tree, &service);
	if(found)
		return service.service.tree;
	else
		return 0;
}


void RegistryRemoveTable(Atom form)
{
	Service key = createBTreeService(form, 0);
	BTreeDelete(registry.tree, &key);
}


Service RegistryAddBytecodeService(Atom bytecode)
{
	Service service;
	Atom signature = BytecodeGetSignature(bytecode);
	service.form = FormulaGetForm(signature);
	service.parameters = FormulaGetActors(signature);
	service.type = SERVICE_BYTECODE;
	service.service.bytecode = bytecode;
	// TODO: handle the case of existing service
	ASSERT(BTreeInsert(registry.tree, &service) == BTREE_INSERTED)
	AcquireAtom(bytecode);

	return service;
}


void RegistryRemoveBytecodeService(Atom form)
{
	// TODO:
	ASSERT(false);
}


Service RegistryFindService(Atom form)
{
	Service service = {0};
	service.form = form;
	
	Service * storedService = BTreePeekItem(registry.tree, &service);
	if(storedService)
		service = *storedService;
	// else service will be invalid
	return service;
}
