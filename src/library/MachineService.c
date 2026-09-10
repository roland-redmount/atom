
#include "btree/btree.h"
#include "kernel/ifact.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "lang/formula.h"
#include "library/MachineService.h"
#include "memory/allocator.h"
#include "parser/TermBuilder.h"

// A simple provider ID mechanism. We simply hand out increasing numbers
// as provider IDs.

static uint32 nextProviderID = 1;

uint32 RequestProviderID(void)
{
	return nextProviderID++;
}


/**
 * CLAUDE: A second index over the service registry, holding one ProviderService
 * for each service RegisterMachineService() has created. This gives the services
 * of one provider ID without scanning the service registry; see FreeMachineServices().
 *
 * The index is created when the first service is registered, and freed once the
 * last one is removed, so that MachineService.c needs no setup or shutdown call.
 */
typedef struct s_ProviderService {
	uint32 providerID;
	Service service;
} ProviderService;

static BTree * providerServices;


/**
 * CLAUDE: Order ProviderService records by provider ID, then by operator.
 * A key with a null operator is a prefix key matching every service of the provider.
 */
static int8 compareProviderServices(
	ProviderService const * entry, ProviderService const * entryOrKey)
{
	if(entry->providerID < entryOrKey->providerID)
		return -1;
	if(entry->providerID > entryOrKey->providerID)
		return 1;
	if(!entryOrKey->service.op)
		return 0;
	if(entry->service.op < entryOrKey->service.op)
		return -1;
	if(entry->service.op > entryOrKey->service.op)
		return 1;
	return 0;
}


static int8 btreeCompareProviderServices(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareProviderServices(
		(ProviderService const *) item, (ProviderService const *) itemOrKey);
}


/**
 * CLAUDE: Record a registered service under the ID of the provider registering it.
 */
static void addProviderService(uint32 providerID, Service service)
{
	if(!providerServices)
		providerServices = BTreeCreate(
			sizeof(ProviderService),
			btreeCompareProviderServices,
			0	// nothing to deallocate
		);
	ProviderService entry = {.providerID = providerID, .service = service};
	ASSERT(BTreeInsert(providerServices, &entry) == BTREE_INSERTED)
}

/**
 * Read the given parameters (in canonical order), and write the corresponding
 * IOSignature and the indexOrder that orders parameters as 1, 2, ... arity.
 * Returns the corresponding TypeSignature.
 */
static TypeSignature readSignatureParameters(
	TypedTuple const * parameters, index8 indexOrder[], IOSignature * ioSignature)
{
	bool numberSeen[RELATION_MAX_ARITY] = {0};
	byte atomTypes[RELATION_MAX_ARITY];
	byte parameterIO[RELATION_MAX_ARITY];

	for(index8 i = 0; i < parameters->nAtoms; i++) {
		TypedAtom actor = TypedTupleGetElement(parameters, i);
		// every actor must be a parameter
		ASSERT(actor.type == AT_PARAMETER)
		ASSERT(actor.atom.parameter.atomType)
		atomTypes[i] = actor.atom.parameter.atomType;
		parameterIO[i] = actor.atom.parameter.io;

		// a signature numbers its arguments 1 ... arity, each number occurs exactly once
		index8 number = actor.atom.parameter.number;
		index8 index = number - 1;
		ASSERT((number >= 1) && (number <= parameters->nAtoms))
		ASSERT(!numberSeen[index])
		numberSeen[index] = true;
		indexOrder[index] = i;
	}
	*ioSignature = CreateIOSignature(parameterIO, parameters->nAtoms);
	return CreateTypeSignature(atomTypes, parameters->nAtoms);
}


Service RegisterMachineService(char const * signature, MachineOperatorSpec operatorSpec)
{
	// Parse the signature
	Atom term = CStringToTerm(signature);
	FormulaView termView = FormulaGetView(term);
	size8 arity = termView.actors->nAtoms;
	ASSERT(arity <= RELATION_MAX_ARITY)

	// Determined the indexOrder from the parameter numbers
	IOSignature ioSignature;
	index8 indexOrder[RELATION_MAX_ARITY];
	TypeSignature typeSignature = readSignatureParameters(
		termView.actors, indexOrder, &ioSignature);

	// Create the machine operator
	Operator * op = CreateMachineOperator(arity, indexOrder, operatorSpec);

	// Register the service
	Relation relation = CreateRelation(termView.form, typeSignature);
	Service service = CreateService(relation, ioSignature, op);
	ReleaseRelation(relation);
	ReleaseFormula(term);

	addProviderService(operatorSpec.providerID, service);
	return service;
}


void FreeMachineServices(uint32 providerID)
{
	if(!providerServices)
		return;

	/*
	 * CLAUDE: The key matches every service of the provider, so each lookup gives one
	 * of them. The entry read back is an exact key, which is what BTreeDelete() requires.
	 * An entry is removed from the index before its service is removed from the
	 * service registry, since RemoveService() frees the operator the entry is keyed by.
	 */
	ProviderService key = {.providerID = providerID};
	ProviderService entry;
	while(BTreeGetItem(providerServices, &key, &entry)) {
		ASSERT(BTreeDelete(providerServices, &entry, 0) == BTREE_DELETED)
		RemoveService(entry.service.relation, entry.service.op);
	}

	// CLAUDE: the index is created on demand, so free it once no service is registered
	if(!BTreeNItems(providerServices)) {
		BTreeFree(providerServices);
		providerServices = 0;
	}
}
