#include "kernel/MixedTypeRelation.h"
#include "kernel/multiset.h"
#include "lang/TermForm.h"
#include "library/MachineService.h"
#include "library/reflect.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "memory/references.h"

/**
 * (formula #1<FORMULA arity #2>INT)
 * 
 * The arity of a reflected formula
 */
static bool formulaArityCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[1]._int = FormulaArity(arguments[0]);
	return true;
}

/**
 * (query #1<FORMULA relation #2>RELATION)
 */
static bool queryRelationCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[1] = arguments[0];
	return true;
}


/**
 * (relation #1<RELATION size #2>INT)
 * 
 * Perform the query given by the formula held by the
 * RELATION atom, count the rows and return the result.
 */
static bool relationSizeCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	// A RELATON atom stores the same atom (hash value) as a FORMULA
	FormulaView query = FormulaGetView(arguments[0]);
	MixedTypeRelation * relation = CreateConcatRelation(query);
	int64 nTuples = 0;
	while(MixedTypeRelationNext(relation))
		nTuples++;
	FreeMixedTypeRelation(relation);
	arguments[1]._int = nTuples;
	return true;
}

/**
 * (relation #1<RELATION role #2<NAME actor #3>FLOAT)
 * 
 * Enumerate the actors in role x of the given relation.
 * This is the basis for aggregating relations like sum, mean.
 */

typedef struct s_RelationRoleActorState {
	MixedTypeRelation * relation;
	uint8 actorIndex;
} RelationRoleActorState ;


static void relationRoleActorSetup(void * _state, Atom arguments[], void * readerData, void * storage)
{
	FormulaView query = FormulaGetView(arguments[0]);
	if(!IsTermForm(query.form))
		return;		// TODO: what about a conjunction form?

	RelationRoleActorState * state = _state;
	// Get the position of the first occurence of the role
	Atom predicateForm = TermFormGetPredicateForm(query.form);
	uint8 actorPosition = PredicateFindRolePosition(predicateForm, arguments[1]);
	if(!actorPosition)
		return;		// role does not exist in the given relation
	state->actorIndex = actorPosition - 1;
	state->relation = CreateConcatRelation(query);
}

static bool relationRoleActorCall(void * _state, Atom arguments[], void * readerData, void * storage)
{
	RelationRoleActorState * state = _state;
	if(!state->relation)
		return false;
	while(MixedTypeRelationNext(state->relation)) {
		TypedTuple const * tuple = MixedTypeRelationPeekTuple(state->relation);
		TypedAtom actor = TypedTupleGetElement(tuple, state->actorIndex);
		// NOTE: only FLOAT actors for now
		if(actor.type == AT_FLOAT) {
			arguments[2] = actor.atom;
			return true;
		}
	}
	return false;
}

static void relationRoleActorFinalize(void * _state, void * readerData, void * storage)
{
	RelationRoleActorState * state = _state;
	if(state->relation)
		FreeMixedTypeRelation(state->relation);
}

/**
 * (relation #1<RELATION role #2<NAME sum #3>FLOAT)
 * 
 * Compute the sum of the actors in the given role of the given relation.
 * This is a special-purpose method, as a stop-gap in absence of a generic
 * aggregation method.
 */
static bool relationRoleSumCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	RelationRoleActorState ownState;
	SetMemory(&ownState, sizeof(RelationRoleActorState), 0);
	relationRoleActorSetup(&ownState, arguments, readerData, storage);
	if(!ownState.relation)
		return false;
	// Iterate over the relation and compute the sum
	float sum = 0;
	while(MixedTypeRelationNext(ownState.relation)) {
		TypedTuple const * tuple = MixedTypeRelationPeekTuple(ownState.relation);
		TypedAtom actor = TypedTupleGetElement(tuple, ownState.actorIndex);
		if(actor.type == AT_FLOAT)
			sum += actor.atom._float;
	}
	FreeMixedTypeRelation(ownState.relation);
	arguments[2]._float = sum;
	return true;
}


static NamedFunction const reflectFunctions[] = {
	{"reflect.formulaArityCall", (AnyFunction) formulaArityCall},
	{"reflect.queryRelationCall", (AnyFunction) queryRelationCall},
	{"reflect.relationSizeCall", (AnyFunction) relationSizeCall},
	{"reflect.relationRoleActorSetup", (AnyFunction) relationRoleActorSetup},
	{"reflect.relationRoleActorCall", (AnyFunction) relationRoleActorCall},
	{"reflect.relationRoleActorFinalize", (AnyFunction) relationRoleActorFinalize},
	{"reflect.relationRoleSumCall", (AnyFunction) relationRoleSumCall},
};


void RegisterReflectFunctions(void)
{
	RegisterFunctions(reflectFunctions, sizeof(reflectFunctions) / sizeof(NamedFunction));
}


typedef struct s_ReflectLibrary {
	uint32 moduleID;
} ReflectLibrary;


static ReflectLibrary * reflectLibrary = 0;

void ReflectionSetup(void)
{
	reflectLibrary = Allocate(sizeof(ReflectLibrary));
	SetPersistentState(STATE_KEY_REFLECT, reflectLibrary);

	reflectLibrary->moduleID = RequestModuleID();

	RegisterMachineService(reflectLibrary->moduleID, "formula #1<FORMULA arity #2>INT", formulaArityCall);

	RegisterMachineService(reflectLibrary->moduleID, "query #1<FORMULA relation #2>RELATION", queryRelationCall);

	RegisterMachineService(reflectLibrary->moduleID, "relation #1<RELATION size #2>INT", relationSizeCall);

	RegisterMachineServiceWithState(
		reflectLibrary->moduleID, "relation #1<RELATION role #2<NAME actor #3>FLOAT",
		sizeof(RelationRoleActorState),
		relationRoleActorSetup, relationRoleActorCall, relationRoleActorFinalize
	);

	RegisterMachineService(reflectLibrary->moduleID, "relation #1<RELATION role #2<NAME sum #3>FLOAT", relationRoleSumCall);
}


void ReflectionRestore(void)
{
	reflectLibrary = GetPersistentState(STATE_KEY_REFLECT);
	ASSERT(reflectLibrary)
}


void ReflectionShutdown(void)
{
	FreeModuleRelations(reflectLibrary->moduleID);

	Free(reflectLibrary);
	SetPersistentState(STATE_KEY_REFLECT, 0);
	reflectLibrary = 0;
}
