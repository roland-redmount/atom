
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/operator.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "library/MachineService.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


/**
 * Combine the two inputs with distinct weights, so that reading an argument from the
 * wrong column gives a different result rather than a coincidentally equal one.
 */
static bool weigh(Atom arguments[], void * state, bool isFirstCall)
{
	arguments[2]._int = 100 * arguments[0]._int + 10 * arguments[1]._int;
	return true;
}


static index8 roleIndex(Atom predicateForm, char const * roleName)
{
	Atom role = CreateNameFromCString(roleName);
	index8 index = PredicateRoleIndex(predicateForm, role);
	NameRelease(role);
	return index;
}


/**
 * A machine function is written in the argument order of its signature, while the
 * relation stores its arguments in the canonical role order of its form. This test
 * uses roles whose canonical order is not the order the signature writes them in,
 * which is what makes the permutation observable.
 */
static void testMachineServiceArgumentOrder(void)
{
	Service service = RegisterMachineService(
		"first @1<INT second @2<INT result @3>INT", &weigh, 0);

	Atom predicateForm = service.relation->predicateForm;
	index8 firstIndex = roleIndex(predicateForm, "first");
	index8 secondIndex = roleIndex(predicateForm, "second");
	index8 resultIndex = roleIndex(predicateForm, "result");

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
	// 340 rather than 430, which is what reading the inputs in column order would give
	ASSERT_INT64_EQUAL(arguments[resultIndex]._int, 340)
	// the inputs are returned unchanged
	ASSERT_INT64_EQUAL(arguments[firstIndex]._int, 3)
	ASSERT_INT64_EQUAL(arguments[secondIndex]._int, 4)

	// a machine function computes at most one tuple
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	FreeMachineServices();
}


/**
 * A function returning false yields no tuple, which is how a test is written as a
 * machine service. This one has no output argument at all.
 */
static bool even(Atom arguments[], void * state, bool isFirstCall)
{
	return (arguments[0]._int % 2) == 0;
}


static void testMachineServiceTestPredicate(void)
{
	Service service = RegisterMachineService("even @1<INT", &even, 0);

	Atom arguments[1] = {(Atom) {._int = 4}};
	OperatorContext * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	arguments[0] = (Atom) {._int = 7};
	context = OperatorCreateContext(service.op, arguments);
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	FreeMachineServices();
}


/**
 * A function with a state is called until it says it has no more tuples, so that a
 * service can compute a relation of several tuples. This one counts from @1 to @3,
 * which ascends, so its tuples are ordered as its signature says they are.
 */
typedef struct {
	int64 next;
} CountState;


static bool count(Atom arguments[], void * state, bool isFirstCall)
{
	CountState * countState = state;
	if(isFirstCall)
		countState->next = arguments[0]._int;
	if(countState->next > arguments[2]._int)
		return false;
	arguments[1]._int = countState->next++;
	return true;
}


static void testMachineServiceIterator(void)
{
	Service service = RegisterMachineService(
		"from @1<INT count @2>INT to @3<INT", &count, sizeof(CountState));

	Atom predicateForm = service.relation->predicateForm;
	index8 fromIndex = roleIndex(predicateForm, "from");
	index8 countIndex = roleIndex(predicateForm, "count");
	index8 toIndex = roleIndex(predicateForm, "to");

	// A service that may yield several tuples declares the order it yields them in.
	// Declaring none would claim it yields at most one; see the contract in operator.h
	ASSERT_NOT_NULL(service.op->indexOrder)

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

	// A range holding nothing yields nothing on the very first call, which is where an
	// iterator and a single-tuple function that computes nothing look alike
	arguments[fromIndex] = (Atom) {._int = 5};
	arguments[toIndex] = (Atom) {._int = 1};
	context = OperatorCreateContext(service.op, arguments);
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	FreeMachineServices();
}


/**
 * The state belongs to one evaluation, not to the service, so two evaluations of the
 * same service count independently. This is what a join relies on, evaluating its right
 * child afresh for every tuple of its left child.
 */
static void testMachineServiceIteratorState(void)
{
	Service service = RegisterMachineService(
		"from @1<INT count @2>INT to @3<INT", &count, sizeof(CountState));

	Atom predicateForm = service.relation->predicateForm;
	index8 fromIndex = roleIndex(predicateForm, "from");
	index8 countIndex = roleIndex(predicateForm, "count");
	index8 toIndex = roleIndex(predicateForm, "to");

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
	FreeMachineServices();
}


/**
 * Two services of the same relation differ only in their parameter IO, so registering
 * the second finds the relation the first created rather than creating another.
 */
static bool sum(Atom arguments[], void * state, bool isFirstCall)
{
	arguments[2]._int = arguments[0]._int + arguments[1]._int;
	return true;
}

static bool difference(Atom arguments[], void * state, bool isFirstCall)
{
	arguments[1]._int = arguments[2]._int - arguments[0]._int;
	return true;
}


static void testMachineServiceSharedRelation(void)
{
	size32 nTablesInitial = RelationRegistryNTables();

	Service adding = RegisterMachineService(
		"term @1<INT term @2<INT total @3>INT", &sum, 0);
	ASSERT_UINT32_EQUAL(RelationRegistryNTables(), nTablesInitial + 1)

	Service subtracting = RegisterMachineService(
		"term @1<INT term @2>INT total @3<INT", &difference, 0);
	// the second service shares the relation of the first
	ASSERT_UINT32_EQUAL(RelationRegistryNTables(), nTablesInitial + 1)
	ASSERT_PTR_EQUAL(subtracting.relation, adding.relation)
	ASSERT_PTR_NOT_EQUAL(subtracting.op, adding.op)

	// dispatch tells them apart by what the query binds
	Formula * query = CStringToTerm("term 3 term 4 total _t");
	Service dispatched;
	index8 permutation[3];
	ASSERT_TRUE(DispatchQueryFormula(query, &dispatched, permutation))
	ASSERT_PTR_EQUAL(dispatched.op, adding.op)
	FreeFormula(query);

	query = CStringToTerm("term 3 term _u total 10");
	ASSERT_TRUE(DispatchQueryFormula(query, &dispatched, permutation))
	ASSERT_PTR_EQUAL(dispatched.op, subtracting.op)
	FreeFormula(query);

	FreeMachineServices();
	ASSERT_UINT32_EQUAL(RelationRegistryNTables(), nTablesInitial)
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testMachineServiceArgumentOrder);
	ExecuteTest(testMachineServiceTestPredicate);
	ExecuteTest(testMachineServiceIterator);
	ExecuteTest(testMachineServiceIteratorState);
	ExecuteTest(testMachineServiceSharedRelation);

	KernelShutdown();

	TestSummary();
}
