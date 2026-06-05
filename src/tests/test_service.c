
#include "kernel/dispatch.h"
#include "kernel/service.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "lang/Formula.h"
#include "lang/Variable.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"


void testMachineExpression(void)
{
	/**
	 * A EXPRESSION_MACHINE for the (+ @INT + @INT = $INT) service
	 */

	Atom query = CStringToPredicate("+ 3 + _ = 7");
	ServiceRecord record;
	index8 argumentMap[3];
	ASSERT(DispatchQueryFormula(query, &record, argumentMap))

	// TODO

	IFactRelease(query);	
}


/**
 * Test creating and executing the JOIN service
 * (multiple m element e multiset p) & (predicate-form p)
 * with arguments (p, e, m)
 */
void testJoinExpression1(void)
{
	ServiceRecord const * leftServiceRecord = RegistryGetCoreServiceRecord(FORM_MULTISET_ELEMENT_MULTIPLE);
	// Left service: (multiple m element e multiset p)
	// This is the same machine service found in the registry,
	// but with a different argument map
	Service leftService;
	SetupMachineService(
		&leftService, 3,
		(index8[]) {2, 1, 0},
		&leftServiceRecord->service.value.machineService
	);

	// Right expression: (predicate-form p)
	// NOTE: this should be identical to the registry service
	ServiceRecord const * rightServiceRecord = RegistryGetCoreServiceRecord(FORM_PREDICATE_FORM);
	Service rightService;
	SetupMachineService(
		&rightService, 1,
		(index8[]) {0},
		&rightServiceRecord->service.value.machineService
	);
	
	// Create the JOIN service, with default argument map
	Service joinService;
	SetupJoinService(&joinService, 3, 0,	&leftService,&rightService);

	// Evaluate with arguments (@multiset-form, _ , _)
	Tuple * arguments = CreateTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_ID, GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT)),
			anonymousVariable,
			anonymousVariable
		},
		3
	);
	// PrintTuple(arguments);
	// PrintChar('\n');
	// Setup execution context
	void * context = ServiceCreateContext(&joinService, arguments);

	// Call the service.
	// This should yield 3 tuples corresponding to the 3 roles of (list position element),
 	// since the right child service (predicate-form @multiset-form) matches a single tuple.
	size32 nElements = 0;
	while(ServiceCall(context)) {
		PrintTuple(arguments);
		PrintChar('\n');
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 3);
	ServiceFreeContext(context);
	FreeTuple(arguments);
}


/**
 * Test evaluating the JOIN service
 * (position p list l element s) & (position q list s element e)
 * with arguments (l p s q e)
 */
void testJoinExpression2(void)
{
	ServiceRecord const * listServiceRecord = RegistryGetCoreServiceRecord(FORM_LIST_POSITION_ELEMENT);
	
	// The left and right child services call the same machine service,
	// but with different argument maps.
	Service leftService;
	SetupMachineService(
		&leftService, 3,
		(index8[]) {1, 0, 2},
		&listServiceRecord->service.value.machineService
	);
	Service rightService;
	SetupMachineService(
		&rightService, 3,
		(index8[]) {3, 2, 4},
		&listServiceRecord->service.value.machineService
	);

	// Create the join service
	Service joinService;
	SetupJoinService(&joinService, 5, 0, &leftService, &rightService);

	// Arguments tuple (@stringList _  _ _ _)
	TypedAtom string1 = CreateTypedAtom(AT_ID, CreateStringFromCString("foo"));
	TypedAtom string2 = CreateTypedAtom(AT_ID, CreateStringFromCString("bar"));
	TypedAtom stringList = CreateTypedAtom(AT_ID, CreateListFromArray((TypedAtom[]) {string1, string2}, 2));
	Tuple * arguments = CreateTuple(5);
	TupleSetElement(arguments, 0, stringList);
	for(index8 i = 1; i < 5; i++)
		TupleSetElement(arguments, i, anonymousVariable);
	PrintTuple(arguments);
	PrintChar('\n');

	// Setup execution context
	void * context = ServiceCreateContext(&joinService, arguments);
	// Call the join service
	size32 nElements = 0;
	while(ServiceCall(context)) {
		PrintTuple(arguments);
		PrintChar('\n');
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 6)
	ServiceFreeContext(context);
	FreeTuple(arguments);
	ReleaseTypedAtom(stringList);
	ReleaseTypedAtom(string1);
	ReleaseTypedAtom(string2);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	// ExecuteTest(testMachineExpression);
	ExecuteTest(testJoinExpression1);
	ExecuteTest(testJoinExpression2);

	KernelShutdown();

	TestSummary();
}

