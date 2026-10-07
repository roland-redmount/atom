#include "kernel/ClosedRelation.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/TupleStore.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "storage/StorageProvider.h"


/*
 * The relation (closed-relation f<ID>) and its operators, stored in the persistent state slot
 * STATE_KEY_CLOSED_RELATIONS.
 */
typedef struct s_ClosedRelations {
	Relation relation;
	// The operator of the service (closed-relation f<ID), testing whether a form is closed
	Operator * testOperator;
	// The operator of the service (closed-relation f>ID), enumerating the closed forms
	Operator * enumerateOperator;
} ClosedRelations;


static ClosedRelations * closedRelations = 0;


void SetupClosedRelations(void)
{
	ClosedRelations * state = Allocate(sizeof(ClosedRelations));

	// Create the (closed-relation) term for
	Atom roleName = CreateNameFromCString("closed-relation");
	Atom predicateForm = CreatePredicateForm(&roleName, 1);
	Atom termForm = CreateTermForm(predicateForm, true);
	IFactRelease(predicateForm);
	NameRelease(roleName);

	// Create the relation and its TupleSTore
	state->relation = (Relation) {
		.form = termForm,
		.typeSignature = CreateTypeSignature((byte[]) {AT_ID}, 1)
	};
	CreateTupleStore(state->relation, GetStorageProvider(PROVIDER_BTREE), 1, 0);
	// The relation now holds a reference to the (closed-relation) term form
	IFactRelease(termForm);

	// Store the operators
	state->testOperator = ServiceGetOperator((Service) {
		.relation = state->relation,
		.ioSignature = CreateIOSignature((byte[]) {PARAMETER_IN}, 1)
	});
	state->enumerateOperator = ServiceGetOperator((Service) {
		.relation = state->relation,
		.ioSignature = CreateIOSignature((byte[]) {PARAMETER_OUT}, 1)
	});
	ASSERT(state->testOperator && state->enumerateOperator)

	closedRelations = state;
	SetPersistentState(STATE_KEY_CLOSED_RELATIONS, closedRelations);
}


void RestoreClosedRelations(void)
{
	closedRelations = GetPersistentState(STATE_KEY_CLOSED_RELATIONS);
	ASSERT(closedRelations)
}


void TeardownClosedRelations(void)
{
	ASSERT(RelationNRows(closedRelations->relation) == 0)
	DropRelation(closedRelations->relation);
	Free(closedRelations);
	closedRelations = 0;
	SetPersistentState(STATE_KEY_CLOSED_RELATIONS, 0);
}


Atom GetClosedRelationForm(void)
{
	return closedRelations->relation.form;
}


/*
 * Test whether a typed relation of the given form has a TupleStore.
 */
static bool formHasTupleStore(Atom form)
{
	bool hasTupleStore = false;
	RelationIterator iterator;
	RelationRegistryIterate(form, &iterator);
	while(!hasTupleStore && RelationIteratorNext(&iterator))
		hasTupleStore = (RelationGetTupleStore(RelationIteratorGet(&iterator)) != 0);
	RelationIteratorEnd(&iterator);
	return hasTupleStore;
}


int CloseRelation(Atom form)
{
	if(!IsTermForm(form))
		return CLOSE_NOT_TERM_FORM;
	if(RelationIsClosedForm(form))
		return CLOSE_EXISTED;

	Atom oppositeForm = TermFormCreateOppositeForm(form);
	int result;
	if(RelationIsClosedForm(oppositeForm))
		result = CLOSE_OPPOSITE_CLOSED;
	else if(formHasTupleStore(oppositeForm))
		result = CLOSE_OPPOSITE_STORED;
	else {
		ASSERT(RelationAddTuple(closedRelations->relation, &form, 0) == TUPLE_ADDED)
		LookupAddFactRoles(closedRelations->relation, &form);
		// CLAUDE: The opposite relation is answered by INVERT operators from now on
		InvalidateTermFormServices(oppositeForm, INVALIDATE_BY_RULE);
		result = CLOSE_OK;
	}
	IFactRelease(oppositeForm);
	return result;
}


void OpenRelation(Atom form)
{
	ASSERT(RelationIsClosedForm(form))
	Atom oppositeForm = TermFormCreateOppositeForm(form);
	// CLAUDE: The opposite form is created before the tuple is removed, since the tuple
	// may hold the only reference to the form
	InvalidateTermFormServices(oppositeForm, INVALIDATE_BY_RULE);
	IFactRelease(oppositeForm);
	LookupRemoveFactRoles(closedRelations->relation, &form);
	ASSERT(RelationRemoveTuple(closedRelations->relation, &form, 0) == TUPLE_REMOVED)
}


bool RelationIsClosedForm(Atom form)
{
	if(!closedRelations)
		return false;	// Return false during early bootstrap; see KernelInitialize()

	Atom arguments[1] = {form};
	return OperatorCallOnce(closedRelations->testOperator, arguments);
}


bool RelationIsClosedInferred(Atom form)
{
	if(!closedRelations || !IsTermForm(form))
		return false;
	// CLAUDE: Each closed form is compared with the given form, since creating the opposite
	// form would create a new ifact when the opposite form does not exist
	Atom predicateForm = TermFormGetPredicateForm(form);
	bool sign = TermFormGetSign(form);
	bool isInferred = false;
	Atom arguments[1];
	OperatorContext * context = OperatorCreateContext(closedRelations->enumerateOperator, arguments);
	while(!isInferred && OperatorCall(context)) {
		isInferred = SameAtoms(TermFormGetPredicateForm(arguments[0]), predicateForm)
			&& (TermFormGetSign(arguments[0]) != sign);
	}
	OperatorFreeContext(context);
	return isInferred;
}


bool RelationIsClosed(Atom form)
{
	return RelationIsClosedForm(form) || RelationIsClosedInferred(form);
}
