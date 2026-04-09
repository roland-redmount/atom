
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
#include "vm/bytecode.h"

/**
 * The registry stores all Service entries in a BTree, indexed by form.
 * 
 * TODO: this structure must be persisted.
 */
struct {
	// B-tree for service lookup
	BTree * tree;
	// array of core services for fast lookup
	Service coreServices[N_CORE_PREDICATES + 1];
} registry;


int8 compareTypedParameters(TypedAtom typedAtom1, TypedAtom typedAtom2)
{
	return CompareParameters(typedAtom1.atom, typedAtom2.atom);
}

/**
 * Two services compare equal if (1) both forms and parameters match, or
 * (2) forms match and serviceOrKey is 0.
 */
static int8 compareServices(Service const * service, Service const * serviceOrKey)
{
	int8 formOrder = CompareAtoms(service->form, serviceOrKey->form);
	if(formOrder != 0)
		return formOrder;
	else {
		if(!service->parameters || !serviceOrKey->parameters) {
			// parameters = 0 matches any parameter vector
			return 0;
		}
		else {
			// compare parameters lists
			return ListLexicalOrdering(
				service->parameters, serviceOrKey->parameters,
				&compareTypedParameters
			);
		}
	}
}


static int8 btreeCompareServices(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareServices((Service *) item, (Service *) itemOrKey);
}


static void btreeFreeService(void * item, size32 itemSize)
{
	Service * service = (Service *) item;
	// The first 2 core predicate forms are not referenced
	if(service->form > 2)
		IFactRelease(service->form);
	if(service->parameters)
		IFactRelease(service->parameters);
	switch(service->type) {
		case SERVICE_BTREE: {
			size32 nRows = RelationBTreeNRows(service->service.tree);
			if(nRows > 0) {
				;
				ASSERT(false)
			}
			FreeRelationBTree(service->service.tree);
			break;
		}
		case SERVICE_BYTECODE:
		IFactRelease(service->service.bytecode);
		break;

		default:
		break;
	}
}


void SetupRegistry(void)
{
	registry.tree = BTreeCreate(
	    sizeof(Service),
	    btreeCompareServices,
	    btreeFreeService
	);
	SetMemory(registry.coreServices, (N_CORE_PREDICATES + 1) * sizeof(Service), 0);
}


size32 RegistryNServices(void)
{
	return BTreeNItems(registry.tree);
}


static TypedAtom btreeParameterGenerator(index32 index, void const * data)
{
	return CreateParameter(PARAMETER_IN_OUT, AT_NONE);
}

static Atom btreeParameterList(size8 arity)
{
	return CreateList(btreeParameterGenerator, 0, arity);
}

BTree * RegistryGetCoreTable(index32 index)
{
	return registry.coreServices[index].service.tree;
}


void FreeRegistry(void)
{
	// Only the first 2 core services are never removed
	ASSERT(BTreeNItems(registry.tree) == 2)
	BTreeFree(registry.tree);
}


static void addService(Service const * service)
{
	// TODO: handle the case of existing service
	ASSERT(BTreeInsert(registry.tree, service) == BTREE_INSERTED)
}


void RegistryAddCoreBTreeService(index32 index, Atom form, size8 arity)
{
	ASSERT(index >= 1);
	ASSERT(index <= N_CORE_PREDICATES)

	registry.coreServices[index] = (Service) {
		.type = SERVICE_BTREE,
		.form = form,
		.parameters = 0,
		.service.tree = CreateRelationBTree(arity)
	};
	addService(&(registry.coreServices[index]));
	// While bootstrapping the first 2 core services, we cannot use IFactAcuiqre()
	if(index > 2)
		IFactAcquire(form);
}


void RegistryRemoveCoreBTreeService(index32 index)
{
	ASSERT(index > 2)
	Service * service = &(registry.coreServices[index]);
	ASSERT(BTreeDelete(registry.tree, service) == BTREE_DELETED)
	SetMemory(service, sizeof(Service), 0);
}


Service RegistryAddBTreeService(Atom form, BTree * btree)
{
	Service service = {
		.type = SERVICE_BTREE,
		.form = form,
		// For a B-tree service, all parameters are in/out
		.parameters = 0,
		.service.tree = btree
	};
	addService(&service);
	IFactAcquire(form);
	return service;
}


Service RegistryAddBytecodeService(Atom signature, Atom bytecode)
{
	ASSERT(IsFormula(signature))
	Service service = {
		.form = FormulaGetForm(signature),
		.parameters = FormulaGetActors(signature),
		.type = SERVICE_BYTECODE,
		.service.bytecode = bytecode
	};
	addService(&service);
	IFactAcquire(service.form);
	IFactAcquire(service.parameters);
	IFactAcquire(service.service.bytecode);
	return service;
}


void RegistryRemoveService(Service service)
{
	ASSERT(BTreeDelete(registry.tree, &service) == BTREE_DELETED);
}


Service RegistryFindService(Atom signature)
{
	Service service = {
		.form = FormulaGetForm(signature),
		.parameters = FormulaGetActors(signature)
	};
	if(!BTreeGetItem(registry.tree, &service))
		service = (Service) {0};
	return service;
}


Service RegistryFindBTreeService(Atom form)
{
	Service service = {
		.form = form,
		.parameters = 0	// match any parameter vector
	};
	// TODO: this query might match multiple services ...
	if(!BTreeGetItem(registry.tree, &service))
		service = (Service) {0};
	else
		ASSERT(service.type == SERVICE_BTREE)
	return service;
}

Atom ServiceCreateSignature(Service const * service)
{
	size8 arity = FormArity(service->form);
	// TODO:  This does not work!  The signature won't match
	// any service in the registry since the parameter list should be zero
	Atom parameters = service->parameters ?
		service->parameters :
		btreeParameterList(arity);
	return CreateFormula(service->form, service->parameters);
}
