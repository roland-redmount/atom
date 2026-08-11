
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
static bool weigh(Atom arguments[])
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
		"first @1<INT second @2<INT result @3>INT", &weigh);

	Atom predicateForm = service.relation->predicateForm;
	index8 firstIndex = roleIndex(predicateForm, "first");
	index8 secondIndex = roleIndex(predicateForm, "second");
	index8 resultIndex = roleIndex(predicateForm, "result");

	// The test only has teeth while the canonical order differs from the signature
	// order. Should these roles ever hash into the signature order, pick other names.
	ASSERT_FALSE((firstIndex == 0) && (secondIndex == 1) && (resultIndex == 2))

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
static bool even(Atom arguments[])
{
	return (arguments[0]._int % 2) == 0;
}


static void testMachineServiceTestPredicate(void)
{
	Service service = RegisterMachineService("even @1<INT", &even);

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
 * Two services of the same relation differ only in their parameter IO, so registering
 * the second finds the relation the first created rather than creating another.
 */
static bool sum(Atom arguments[])
{
	arguments[2]._int = arguments[0]._int + arguments[1]._int;
	return true;
}

static bool difference(Atom arguments[])
{
	arguments[1]._int = arguments[2]._int - arguments[0]._int;
	return true;
}


static void testMachineServiceSharedRelation(void)
{
	size32 nTablesInitial = RelationRegistryNTables();

	Service adding = RegisterMachineService(
		"term @1<INT term @2<INT total @3>INT", &sum);
	ASSERT_UINT32_EQUAL(RelationRegistryNTables(), nTablesInitial + 1)

	Service subtracting = RegisterMachineService(
		"term @1<INT term @2>INT total @3<INT", &difference);
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
	ExecuteTest(testMachineServiceSharedRelation);

	KernelShutdown();

	TestSummary();
}
