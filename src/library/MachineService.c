
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
	RelationSignature relation;
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
static void addModuleRelation(uint32 moduleID, RelationSignature relation)
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
	Atom term = CStringToTerm(signature);
	FormulaView termView = FormulaGetView(term);
	size8 arity = termView.actors->nAtoms;
	ASSERT(arity <= RELATION_MAX_ARITY)

	// Determine the indexOrder, type signature and IO signature from the parameter numbers
	IOSignature ioSignature;
	index8 indexOrder[RELATION_MAX_ARITY];
	TypeSignature typeSignature = readSignatureParameters(
		termView.actors, indexOrder, &ioSignature);
	
	// Create the relation, unless it already exists
	RelationSignature relation = {.termForm = termView.form, .typeSignature = typeSignature};
	if(RelationExists(relation)) {
		// TODO: verify that the relation's provider matches ours,
		// and the index order matches
	}
	else {
		relation = CreateRelation(termView.form, typeSignature, &defaultProvider, indexOrder);
		addModuleRelation(moduleID, relation);
	}
	ReleaseFormula(term);
	
	// Add the new reader to the relation
	RelationReaderSpec readerSpec = {
		.stateSize = stateSize,
		.setupState = setupState,
		.call = call,
		.finalizeState = finalizeState,
		.ioSignature = ioSignature
	};
	return RelationAddPrimitiveService(relation, &readerSpec);
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
