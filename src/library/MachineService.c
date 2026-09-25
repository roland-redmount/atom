
#include "btree/btree.h"
#include "kernel/ifact.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/TupleStore.h"
#include "lang/formula.h"
#include "library/MachineService.h"
#include "memory/allocator.h"
#include "parser/FormulaBuilder.h"


static uint32 nextModuleID = 1;

uint32 RequestModuleID(void)
{
	return nextModuleID++;
}


/**
 * An index associating modules with their registered relations.
 */
typedef struct s_ModuleRelation {
	uint32 moduleID;
	Relation relation;
} ModuleRelation;

static BTree * moduleRelations;


/**
 * Order ModuleRelation records by module ID, then by relation.
 * A key with a null relation is a prefix key matching every relation of the module.
 */
static int8 compareModuleRelations(
	ModuleRelation const * entry, ModuleRelation const * entryOrKey)
{
	if(entry->moduleID < entryOrKey->moduleID)
		return -1;
	if(entry->moduleID > entryOrKey->moduleID)
		return 1;
	if(IsNullRelation(entryOrKey->relation))
		return 0;
	else
		return CompareRelations(entry->relation, entryOrKey->relation);
}


static int8 btreeCompareModuleRelations(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareModuleRelations(
		(ModuleRelation const *) item, (ModuleRelation const *) itemOrKey);
}


/**
 * Associate a registered relation with a module ID
 */
static void addModuleRelation(uint32 moduleID, Relation relation)
{
	// Create B-tree on first call
	if(!moduleRelations) {
		moduleRelations = BTreeCreate(
			sizeof(ModuleRelation),
			btreeCompareModuleRelations,
			0	// nothing to deallocate
		);
	}
	// Add the module-relation pair
	ModuleRelation entry = {.moduleID = moduleID, .relation = relation};
	ASSERT(BTreeInsert(moduleRelations, &entry) == BTREE_INSERTED)
}


/**
 * Read the given parameters (in canonical order), and write the corresponding
 * IOSignature and the indexOrder that orders parameters as 1, 2, ... nArguments.
 * A parameter may be repeated, as in (foo @2>INT bar @1<INT & bar @2>INT baz @3<INT).
 * Repeated parameters are written to *equalitySignature.
 * The service operator then takes one argument per distinct parameter,
 * and the indexOrder has one entry per distinct parameter: indexOrder[number - 1] is the
 * operator argument taken by the parameter with that number.
 * 
 * Returns the corresponding TypeSignature.
 */
static TypeSignature readSignatureParameters(
	TypedTuple const * parameters, index8 indexOrder[], IOSignature * ioSignature,
	EqualitySignature * equalitySignature)
{
	bool numberSeen[RELATION_MAX_ARITY] = {0};
	byte atomTypes[RELATION_MAX_ARITY];
	byte parameterIO[RELATION_MAX_ARITY];

	*equalitySignature = ParametersGetEqualitySignature(
		TypedTuplePeekAtoms(parameters), parameters->nAtoms);
	index8 argumentMap[RELATION_MAX_ARITY];
	// nArguments equals the number of distinct parameters
	size8 nArguments = EqualitySignatureGetArgumentMap(
		*equalitySignature, parameters->nAtoms, argumentMap);

	for(index8 i = 0; i < parameters->nAtoms; i++) {
		TypedAtom actor = TypedTupleGetElement(parameters, i);
		// every actor must be a parameter
		ASSERT(actor.type == AT_PARAMETER)
		ASSERT(actor.atom.parameter.atomType)
		atomTypes[i] = actor.atom.parameter.atomType;
		parameterIO[i] = actor.atom.parameter.io;

		// A repeated parameter must always have the type and IO direction
		uint8 repeatOf = equalitySignature->repeatOf[i];
		if(repeatOf) {
			ASSERT(atomTypes[i] == atomTypes[repeatOf - 1])
			ASSERT(parameterIO[i] == parameterIO[repeatOf - 1])
			continue;
		}

		// a signature numbers its arguments 1 ... nArguments
		index8 number = actor.atom.parameter.number;
		index8 index = number - 1;
		ASSERT((number >= 1) && (number <= nArguments))
		ASSERT(!numberSeen[index])
		numberSeen[index] = true;
		indexOrder[index] = argumentMap[i];
	}
	*ioSignature = CreateIOSignature(parameterIO, parameters->nAtoms);
	return CreateTypeSignature(atomTypes, parameters->nAtoms);
}


Service RegisterMachineService(
	uint32 moduleID, char const * signature,
	bool (*call)(void *, Atom [], void *, void *))
{
	return RegisterMachineServiceWithState(moduleID, signature, 0, 0, call, 0);
}


Service RegisterMachineServiceWithState(
	uint32 moduleID, char const * signature, size32 stateSize,
	void (*setupState)(void *, Atom[], void *, void *),
	bool (*call)(void *, Atom[], void *, void *),
	void (*finalizeState)(void *, void *, void *))
{
	// Parse the signature
	// CLAUDE: The signature is a term or a conjunction of terms
	Atom term = CStringToFormula(signature);
	FormulaView termView = FormulaGetView(term);
	ASSERT(IsRelationForm(termView.form))
	size8 arity = termView.actors->nAtoms;
	ASSERT(arity <= RELATION_MAX_ARITY)

	// Determine the indexOrder, type signature and IO signature from the parameter numbers
	IOSignature ioSignature;
	EqualitySignature equalitySignature;
	index8 indexOrder[RELATION_MAX_ARITY];
	TypeSignature typeSignature = readSignatureParameters(
		termView.actors, indexOrder, &ioSignature, &equalitySignature);
	index8 argumentMap[RELATION_MAX_ARITY];
	size8 nArguments = EqualitySignatureGetArgumentMap(equalitySignature, arity, argumentMap);
	
	// Create the relation, unless it already exists
	Relation relation = {.form = termView.form, .typeSignature = typeSignature};
	TupleStore * store = 0;
	if(RelationExists(relation))
		store = RelationGetTupleStore(relation);
	if(store) {
		// TODO: verify that the store's provider matches ours,
		// and the index order matches
	}
	else {
		// CLAUDE: With repeated parameters the indexOrder has fewer entries than the
		// store has columns, and the store takes the identity column order
		store = CreateTupleStore(
			relation, &defaultProvider, arity, (nArguments == arity) ? indexOrder : 0);
		addModuleRelation(moduleID, relation);
	}
	ReleaseFormula(term);
	
	// Create a new primitive service
	RelationReaderSpec readerSpec = {
		.stateSize = stateSize,
		.setupState = setupState,
		.call = call,
		.finalizeState = finalizeState,
		.ioSignature = ioSignature
	};
	Operator * op = CreateMachineOperator(nArguments, indexOrder, &readerSpec, store->storage);
	Service service = {
		.relation = relation,
		.ioSignature = ioSignature,
		.equalitySignature = equalitySignature
	};
	CreateService(service, op);
	return service;
}


void FreeModuleRelations(uint32 moduleID)
{
	if(!moduleRelations)
		return;

	ModuleRelation key = {.moduleID = moduleID};
	ModuleRelation entry;
	while(BTreeGetItem(moduleRelations, &key, &entry)) {
		ASSERT(BTreeDelete(moduleRelations, &entry, 0) == BTREE_DELETED)
		DropRelation(entry.relation);
	}

	// the index is created on demand, so free it when it becomes empty
	if(BTreeNItems(moduleRelations) == 0) {
		BTreeFree(moduleRelations);
		moduleRelations = 0;
	}
}
