
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


static uint32 moduleID;		// for creating machine operators 

/**
 * A service (first @1<INT second @2<INT result @3>INT)
 * combining the two inputs with distinct weights, so that reading an argument from the
 * wrong column gives a different result rather than a coincidentally equal one.
 */
static bool weighCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._int = 100 * arguments[0]._int + 10 * arguments[1]._int;
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
		moduleID, "first @1<INT second @2<INT result @3>INT", weighCall);
	Operator * operator = FindServiceOperator(service);

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

	OperatorContext * context = OperatorCreateContext(operator, arguments);
	ASSERT_TRUE(OperatorCall(context))
	// 340, not the 430 that reading the inputs in column order would give
	ASSERT_INT64_EQUAL(arguments[resultIndex]._int, 340)
	// the inputs are returned unchanged
	ASSERT_INT64_EQUAL(arguments[firstIndex]._int, 3)
	ASSERT_INT64_EQUAL(arguments[secondIndex]._int, 4)

	// a machine function computes at most one tuple
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	FreeModuleRelations(moduleID);
}


/**
 * A function (even <INT) returning false if the argument it odd (yields no tuple).
 * This one has no output argument at all.
 */
static bool evenCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	return (arguments[0]._int % 2) == 0;
}


static void testMachineServiceTestPredicate(void)
{
	Service service = RegisterMachineService(
		moduleID, "even @1<INT", evenCall);
	Operator * operator = FindServiceOperator(service);

	Atom arguments[1] = {(Atom) {._int = 4}};
	OperatorContext * context = OperatorCreateContext(operator, arguments);
	ASSERT_TRUE(OperatorCall(context))
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	arguments[0] = (Atom) {._int = 7};
	context = OperatorCreateContext(operator, arguments);
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	FreeModuleRelations(moduleID);
}


/**
 * An stateful operator (from @1<INT count @2>INT to @3<INT)
 * This counts from @1 to @3 ascending, so the tuples are ordered as the signature says.
 */
typedef struct {
	int64 next;
} CountState;

static void countSetup(void * state, Atom arguments[], void * readerData, void * storage)
{
	CountState * countState = state;
	countState->next = arguments[0]._int;
}


static bool countCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	CountState * countState = state;
	if(countState->next > arguments[2]._int)
		return false;
	arguments[1]._int = countState->next++;
	return true;
}


static void testMachineServiceIterator(void)
{
	Service service = RegisterMachineServiceWithState(
		moduleID, "from @1<INT count @2>INT to @3<INT",
		sizeof(CountState), countSetup, countCall, 0);

	index8 fromIndex = roleIndex(service.relation.termForm, "from");
	index8 countIndex = roleIndex(service.relation.termForm, "count");
	index8 toIndex = roleIndex(service.relation.termForm, "to");

	// A service declares the order its signature writes its arguments in;
	// see the contract in operator.h
	Operator * operator = FindServiceOperator(service);
	ASSERT_UINT32_EQUAL(operator->indexOrder[0], fromIndex)
	ASSERT_UINT32_EQUAL(operator->indexOrder[1], countIndex)
	ASSERT_UINT32_EQUAL(operator->indexOrder[2], toIndex)

	Atom arguments[3];
	arguments[fromIndex] = (Atom) {._int = 1};
	arguments[toIndex] = (Atom) {._int = 5};
	OperatorContext * context = OperatorCreateContext(operator, arguments);
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
	context = OperatorCreateContext(operator, arguments);
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	FreeModuleRelations(moduleID);
}


/**
 * The state belongs to one evaluation rather than to the service, so two evaluations of
 * one service count independently. A JOIN operator relies on this, evaluating its right
 * child afresh for every tuple of its left child.
 */
static void testMachineServiceIteratorState(void)
{
	Service service = RegisterMachineServiceWithState(
		moduleID, "from @1<INT count @2>INT to @3<INT",
		sizeof(CountState), countSetup, countCall, 0);

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
	Operator * operator = FindServiceOperator(service);
	OperatorContext * firstContext = OperatorCreateContext(operator, first);
	OperatorContext * secondContext = OperatorCreateContext(operator, second);
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
	FreeModuleRelations(moduleID);
}


/**
 * Two services of the same relation differ only in their parameter IO, so registering
 * the second finds the relation the first created rather than creating another.
 */
static bool sumCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[2]._int = arguments[0]._int + arguments[1]._int;
	return true;
}

static bool subtractCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	arguments[1]._int = arguments[2]._int - arguments[0]._int;
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

	// Registering the first service for a new relation creates the relation
	Service sumService = RegisterMachineService(
		moduleID, "term @1<INT term @2<INT sum @3>INT", sumCall);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial + 1)

	Service subtractService = RegisterMachineService(
		moduleID, "term @1<INT term @2>INT sum @3<INT", subtractCall);
	// the second service shares the relation of the first
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial + 1)
	ASSERT_TRUE(SameRelations(subtractService.relation, sumService.relation))
	Operator * sumOperator = FindServiceOperator(sumService);
	Operator * subtractOperator = FindServiceOperator(subtractService);
	ASSERT_PTR_NOT_EQUAL(subtractOperator, sumOperator)

	// dispatch tells the services apart by query argument types
	Atom query = CStringToTerm("term 3 term 4 sum t");
	Service dispatched;
	index8 permutation[3];
	ASSERT_TRUE(DispatchQueryFormula(query, &dispatched, permutation))
	ASSERT_PTR_EQUAL(FindServiceOperator(dispatched), sumOperator)
	ReleaseFormula(query);

	query = CStringToTerm("term 3 term u sum 10");
	ASSERT_TRUE(DispatchQueryFormula(query, &dispatched, permutation))
	ASSERT_PTR_EQUAL(FindServiceOperator(dispatched), subtractOperator)
	ReleaseFormula(query);

	FreeModuleRelations(moduleID);
	ASSERT_UINT32_EQUAL(RelationRegistryNRelations(), nRelationsInitial)
}


int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);

	moduleID = RequestModuleID();

	ExecuteTest(testMachineServiceArgumentOrder);
	ExecuteTest(testMachineServiceTestPredicate);
	ExecuteTest(testMachineServiceIterator);
	ExecuteTest(testMachineServiceIteratorState);
	ExecuteTest(testMachineServiceSharedRelation);

	KernelShutdown();

	TestSummary();
}
