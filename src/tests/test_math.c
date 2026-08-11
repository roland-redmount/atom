#include "kernel/dispatch.h"
#include "kernel/operator.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/MachineService.h"
#include "library/math.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


void testAdd1(void)
{
	Formula * query = CStringToTerm("+ 2 + 3 = _");

	Service service;
	index8 permutation[3];
	ASSERT(DispatchQueryFormula(query, &service, permutation))

	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(query->actors), arguments, 3);
	
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))
	
	Atom equalsRole = CreateNameFromCString("=");
	index8 equalsRoleIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(query->form),
		equalsRole
	);
	NameRelease(equalsRole);
	ASSERT_INT32_EQUAL(arguments[equalsRoleIndex]._int, 2 + 3);

	ASSERT_FALSE(OperatorCall(context))
	
	OperatorFreeContext(context);
	FreeFormula(query);
}


void testAdd2(void)
{
	Formula * query = CStringToTerm("= 7 + 4 + _");

	Service service;
	index8 permutation[3];
	ASSERT(DispatchQueryFormula(query, &service, permutation))

	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(query->actors), arguments, 3);
	
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom plusRole = CreateNameFromCString("+");
	// Get the index of the second '+' role actor in the canonical form
	index8 plusRoleIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(query->form),
		plusRole
	) + 1;
	NameRelease(plusRole);
	// Account for dispatch argument permutation to pick the right actor
	ASSERT_INT32_EQUAL(arguments[permutation[plusRoleIndex]]._uint, 7 - 4);

	ASSERT_FALSE(OperatorCall(context))
	
	OperatorFreeContext(context);
	FreeFormula(query);
}


/**
 * The range service yields one tuple per number in the range, rather than the single
 * tuple the arithmetic services compute.
 */
void testRange(void)
{
	Formula * query = CStringToTerm("lower 2 number _n upper 6");

	Service service;
	index8 permutation[3];
	ASSERT(DispatchQueryFormula(query, &service, permutation))

	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(query->actors), arguments, 3);

	Atom numberRole = CreateNameFromCString("number");
	index8 numberIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(query->form),
		numberRole
	);
	NameRelease(numberRole);

	OperatorContext * context = OperatorCreateContext(service.op, arguments);
	for(int64 expected = 2; expected <= 6; expected++) {
		ASSERT_TRUE(OperatorCall(context))
		ASSERT_INT64_EQUAL(arguments[numberIndex]._int, expected)
	}
	ASSERT_FALSE(OperatorCall(context))

	OperatorFreeContext(context);
	FreeFormula(query);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testAdd1);
	ExecuteTest(testAdd2);
	ExecuteTest(testRange);

	FreeMachineServices();
	KernelShutdown();

	TestSummary();
}

