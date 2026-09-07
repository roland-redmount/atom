
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/operator.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/library.h"
#include "library/MachineService.h"
#include "library/string.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


static uint32 providerID;		// for creating machine operators 

/**
 * A service (first @1<INT second @2<INT result @3>INT)
 * combining the two inputs with distinct weights, so that reading an argument from the
 * wrong column gives a different result rather than a coincidentally equal one.
 */
static bool weigh(MachineOperatorContext * context)
{
	context->arguments[2]._int = 100 * context->arguments[0]._int + 10 * context->arguments[1]._int;
	return true;
}


// NOTE: this should perhaps be provided by TermForom
static index8 roleIndex(Atom termForm, char const * roleName)
{
	Atom role = CreateNameFromCString(roleName);
	Atom predicateForm = TermFormGetPredicateForm(termForm);
	index8 index = PredicateRoleIndex(predicateForm, role);
	NameRelease(role);
	return index;
}


/**
 * A machine function is written in the argument order of its signature, while the
 * relation stores its arguments in canonical role order. The roles here have a
 * canonical order differing from the signature order, so the permutation is observable.
 */
static void testMachineServiceArgumentOrder(void)
{
	Service service = RegisterMachineService(
		providerID, "first @1<INT second @2<INT result @3>INT",
		(MachineOperatorSpec) {.call = weigh}, 0, 0);

	index8 firstIndex = roleIndex(service.relation.termForm, "first");
	index8 secondIndex = roleIndex(service.relation.termForm, "second");
	index8 resultIndex = roleIndex(service.relation.termForm, "result");

	// The test only has teeth while the canonical order differs from the signature
	// order. Should these roles ever hash into the signature order, pick other names.
	ASSERT((firstIndex != 0) || (secondIndex != 1) || (resultIndex != 2))

	// the service takes its inputs in the columns of their roles
	Atom arguments[3];
	arguments[firstIndex] = (Atom) {._int = 3};
	arguments[secondIndex] = (Atom) {._int = 4};
	arguments[resultIndex] = (Atom) {._int = 0};

	OperatorContext * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))
	// 340, not the 430 that reading the inputs in column order would give
	ASSERT_INT64_EQUAL(arguments[resultIndex]._int, 340)
	// the inputs are returned unchanged
	ASSERT_INT64_EQUAL(arguments[firstIndex]._int, 3)
	ASSERT_INT64_EQUAL(arguments[secondIndex]._int, 4)

	// a machine function computes at most one tuple
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	FreeMachineServices(providerID);
}


/**
 * A function (even <INT) returning false if the argument it odd (yields no tuple).
 * This one has no output argument at all.
 */
static bool even(MachineOperatorContext * context)
{
	return (context->arguments[0]._int % 2) == 0;
}


static void testMachineServiceTestPredicate(void)
{
	Service service = RegisterMachineService(
		providerID, "even @1<INT",
		(MachineOperatorSpec) {.call = even}, 0, 0);

	Atom arguments[1] = {(Atom) {._int = 4}};
	OperatorContext * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	arguments[0] = (Atom) {._int = 7};
	context = OperatorCreateContext(service.op, arguments);
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	FreeMachineServices(providerID);
}


/**
 * An stateful operator (from @1<INT count @2>INT to @3<INT)
 * This counts from @1 to @3 ascending, so the tuples are ordered as the signature says.
 */
typedef struct {
	int64 next;
} CountState;

static void countSetup(MachineOperatorContext * context, void * operatorData)
{
	CountState * countState = (CountState *) context->state;
	countState->next = context->arguments[0]._int;
}


static bool countCall(MachineOperatorContext * context)
{
	CountState * countState = (CountState *) context->state;
	if(countState->next > context->arguments[2]._int)
		return false;
	context->arguments[1]._int = countState->next++;
	return true;
}


static void testMachineServiceIterator(void)
{
	Service service = RegisterMachineService(
		providerID, "from @1<INT count @2>INT to @3<INT",
		(MachineOperatorSpec) {.setupState = countSetup, .call = countCall},
		0, sizeof(CountState));

	index8 fromIndex = roleIndex(service.relation.termForm, "from");
	index8 countIndex = roleIndex(service.relation.termForm, "count");
	index8 toIndex = roleIndex(service.relation.termForm, "to");

	// A service declares the order its signature writes its arguments in;
	// see the contract in operator.h
	ASSERT_UINT32_EQUAL(service.op->indexOrder[0], fromIndex)
	ASSERT_UINT32_EQUAL(service.op->indexOrder[1], countIndex)
	ASSERT_UINT32_EQUAL(service.op->indexOrder[2], toIndex)

	Atom arguments[3];
	arguments[fromIndex] = (Atom) {._int = 1};
	arguments[toIndex] = (Atom) {._int = 5};
	OperatorContext * context = OperatorCreateContext(service.op, arguments);
	for(int64 expected = 1; expected <= 5; expected++) {
		ASSERT_TRUE(OperatorCall(context))
		ASSERT_INT64_EQUAL(arguments[countIndex]._int, expected)
	}
	// exhausted, and it stays exhausted
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	// An empty range yields nothing on the first call, as a single-tuple function
	// computing nothing also does
	arguments[fromIndex] = (Atom) {._int = 5};
	arguments[toIndex] = (Atom) {._int = 1};
	context = OperatorCreateContext(service.op, arguments);
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	FreeMachineServices(providerID);
}


/**
 * The state belongs to one evaluation rather than to the service, so two evaluations of
 * one service count independently. A JOIN operator relies on this, evaluating its right
 * child afresh for every tuple of its left child.
 */
static void testMachineServiceIteratorState(void)
{
	Service service = RegisterMachineService(
		providerID, "from @1<INT count @2>INT to @3<INT",
		(MachineOperatorSpec) {.setupState = countSetup, .call = countCall},
		0, sizeof(CountState));

	index8 fromIndex = roleIndex(service.relation.termForm, "from");
	index8 countIndex = roleIndex(service.relation.termForm, "count");
	index8 toIndex = roleIndex(service.relation.termForm, "to");

	Atom first[3];
	first[fromIndex] = (Atom) {._int = 1};
	first[toIndex] = (Atom) {._int = 3};
	Atom second[3];
	second[fromIndex] = (Atom) {._int = 10};
	second[toIndex] = (Atom) {._int = 12};

	// interleave the two, so that one advancing cannot be mistaken for the other
	OperatorContext * firstContext = OperatorCreateContext(service.op, first);
	OperatorContext * secondContext = OperatorCreateContext(service.op, second);
	for(int64 i = 0; i < 3; i++) {
		ASSERT_TRUE(OperatorCall(firstContext))
		ASSERT_INT64_EQUAL(first[countIndex]._int, 1 + i)
		ASSERT_TRUE(OperatorCall(secondContext))
		ASSERT_INT64_EQUAL(second[countIndex]._int, 10 + i)
	}
	ASSERT_FALSE(OperatorCall(firstContext))
	ASSERT_FALSE(OperatorCall(secondContext))

	OperatorFreeContext(secondContext);
	OperatorFreeContext(firstContext);
	FreeMachineServices(providerID);
}


/**
 * Two services of the same relation differ only in their parameter IO, so registering
 * the second finds the relation the first created rather than creating another.
 */
static bool sum(MachineOperatorContext * context)
{
	context->arguments[2]._int = context->arguments[0]._int + context->arguments[1]._int;
	return true;
}

static bool difference(MachineOperatorContext * context)
{
	context->arguments[1]._int = context->arguments[2]._int - context->arguments[0]._int;
	return true;
}


/**
 * Two machine services of one signature share a relation, and that relation is computed:
 * no tuple storage is created anywhere along the way, and the relation goes with the last
 * service naming it.
 */
static void testMachineServiceSharedRelation(void)
{
	size32 nRelationsInitial = RelationRegistryNRelations();
	size32 nTablesInitial = NumberOfRelationTables();

	Service adding = RegisterMachineService(
		providerID, "term @1<INT term @2<INT total @3>INT",
		(MachineOperatorSpec) {.call = sum}, 0, 0);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial + 1)
	// a computed service has no storage to register
	ASSERT_UINT32_EQUAL(NumberOfRelationTables(), nTablesInitial)
	ASSERT_NULL(FindRelationTable(adding.relation))

	Service subtracting = RegisterMachineService(
		providerID, "term @1<INT term @2>INT total @3<INT",
		(MachineOperatorSpec) {.call = difference}, 0, 0);
	// the second service shares the relation of the first
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial + 1)
	ASSERT_TRUE(SameRelations(subtracting.relation, adding.relation))
	ASSERT_PTR_NOT_EQUAL(subtracting.op, adding.op)

	// dispatch tells them apart by what the query binds
	Atom query = CStringToTerm("term 3 term 4 total t");
	Service dispatched;
	index8 permutation[3];
	ASSERT_TRUE(DispatchQueryFormula(query, &dispatched, permutation))
	ASSERT_PTR_EQUAL(dispatched.op, adding.op)
	ReleaseFormula(query);

	query = CStringToTerm("term 3 term u total 10");
	ASSERT_TRUE(DispatchQueryFormula(query, &dispatched, permutation))
	ASSERT_PTR_EQUAL(dispatched.op, subtracting.op)
	ReleaseFormula(query);

	// Removing the services removes the relation with them, which is what lets
	// FreeMachineServices() keep no record of what it registered
	FreeMachineServices(providerID);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial)
	ASSERT_UINT32_EQUAL(NumberOfRelationTables(), nTablesInitial)
}


int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);

	providerID = RequestProviderID();

	ExecuteTest(testMachineServiceArgumentOrder);
	ExecuteTest(testMachineServiceTestPredicate);
	ExecuteTest(testMachineServiceIterator);
	ExecuteTest(testMachineServiceIteratorState);
	ExecuteTest(testMachineServiceSharedRelation);

	KernelShutdown();

	TestSummary();
}
