#include "kernel/dispatch.h"
#include "kernel/operator.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "library/library.h"
#include "library/string.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/MachineService.h"
#include "parser/FormulaBuilder.h"
#include "parser/TermBuilder.h"
#include "ui/query.h"
#include "testing/testing.h"


void testAdd1(void)
{
	Atom query = CStringToTerm("+ 2 + 3 = _");

	Service service;
	index8 permutation[3];
	ASSERT(DispatchQueryFormula(query, &service, permutation))

	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(query)), arguments, 3);
	
	void * context = OperatorCreateContext(ServiceGetOperator(service), arguments);
	ASSERT_TRUE(OperatorCall(context))
	
	Atom equalsRole = CreateNameFromCString("=");
	index8 equalsRoleIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(FormulaGetForm(query)),
		equalsRole
	);
	NameRelease(equalsRole);
	ASSERT_INT32_EQUAL(arguments[equalsRoleIndex]._int, 2 + 3);

	ASSERT_FALSE(OperatorCall(context))
	
	OperatorFreeContext(context);
	ReleaseFormula(query);
}


void testAdd2(void)
{
	Atom query = CStringToTerm("= 7 + 4 + _");

	Service service;
	index8 permutation[3];
	ASSERT(DispatchQueryFormula(query, &service, permutation))

	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(query)), arguments, 3);
	
	void * context = OperatorCreateContext(ServiceGetOperator(service), arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom plusRole = CreateNameFromCString("+");
	// Get the index of the second '+' role actor in the canonical form
	index8 plusRoleIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(FormulaGetForm(query)),
		plusRole
	) + 1;
	NameRelease(plusRole);
	// Account for dispatch argument permutation to pick the right actor
	ASSERT_INT32_EQUAL(arguments[permutation[plusRoleIndex]]._int, 7 - 4);

	ASSERT_FALSE(OperatorCall(context))
	
	OperatorFreeContext(context);
	ReleaseFormula(query);
}


/**
 * The range service yields one tuple per number in the range, rather than the single
 * tuple an arithmetic service computes.
 */
/* CLAUDE: The range service has the conjunction form (=< >= & =< >=), and the query
 * repeats the variable n in both terms. The two terms have the same form, so the query
 * may list them in either order; dispatch finds the service under a permutation. */
static void assertRange(char const * queryString, int64 lower, int64 upper)
{
	Atom query = CStringToFormula(queryString);
	FormulaView queryView = FormulaGetView(query);
	size32 nServices = NumberOfServices();

	Service service;
	index8 permutation[queryView.actors->nAtoms];
	ASSERT_INT32_EQUAL(DispatchQueryFormula(query, &service, permutation), DISPATCH_FOUND)
	ASSERT_TRUE(HasRepeatedParameters(service.equalitySignature))

	// The column of the first occurrence of n
	index8 numberIndex = 0;
	while(TypedTupleGetElement(queryView.actors, numberIndex).type != AT_VARIABLE)
		numberIndex++;

	MixedTypeRelation * relation = UserQuery(queryView);
	for(int64 expected = lower; expected <= upper; expected++) {
		ASSERT_TRUE(MixedTypeRelationNext(relation))
		TypedTuple const * tuple = MixedTypeRelationPeekTuple(relation);
		ASSERT_INT64_EQUAL(TypedTupleGetAtom(tuple, numberIndex)._int, expected)
	}
	ASSERT_FALSE(MixedTypeRelationNext(relation))
	FreeMixedTypeRelation(relation);

	// The query is answered by the primitive service, so nothing was compiled
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices)
	ReleaseFormula(query);
}


void testRange(void)
{
	assertRange("=< n >= 2 & >= n =< 6", 2, 6);
	assertRange(">= n =< 6 & =< n >= 2", 2, 6);
	assertRange("=< n >= 3 & >= n =< 1", 3, 2);
}


int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testAdd1);
	ExecuteTest(testAdd2);
	ExecuteTest(testRange);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}

