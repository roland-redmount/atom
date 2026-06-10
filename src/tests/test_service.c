
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


void testMachineService(void)
{

}


void testPermuteService(void)
{

}


/**
 * Test creating and executing a JOIN service
 * (multiple m element e multiset p) & (predicate-form p)
 * with parent arguments (m, e, p)
 */
void testJoinService1(void)
{
	// Left child services from the registry, no argument permutation
	ServiceRecord const * leftServiceRecord = RegistryGetCoreServiceRecord(FORM_MULTISET_ELEMENT_MULTIPLE);
	// Right service needs a PERMUTE since it takes 1 argument only
	ServiceRecord const * rightServiceRecord = RegistryGetCoreServiceRecord(FORM_PREDICATE_FORM);
	Service * rightService = CreatePermuteService(3, 0, (index8[]) {3}, rightServiceRecord->service);
	
	// Create the JOIN service
	Service * joinService = CreateJoinService(leftServiceRecord->service, rightService);
	ReleaseService(rightService);

	// Evaluate with arguments (@list-form, _ , _)
	Tuple * arguments = CreateTupleFromArray(
		(TypedAtom[]) {
			anonymousVariable,
			anonymousVariable,
			CreateTypedAtom(AT_ID, GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT)),
		},
		3
	);
	PrintTuple(arguments);
	PrintChar('\n');
	// Setup execution context
	void * context = ServiceCreateContext(joinService, arguments);

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
	// This frees the child services
	ReleaseService(joinService);
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
	Service * leftService = CreatePermuteService(5, 0, (index8[]) {2, 1, 3}, listServiceRecord->service);
	Service * rightService = CreatePermuteService(5, 0, (index8[]) {4, 3, 5}, listServiceRecord->service);

	// Create the join service
	Service * joinService = CreateJoinService(leftService, rightService);
	ReleaseService(leftService);
	ReleaseService(rightService);

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
	void * context = ServiceCreateContext(joinService, arguments);
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
	ReleaseService(joinService);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	// ExecuteTest(testMachineExpression);
	ExecuteTest(testJoinService1);
	ExecuteTest(testJoinExpression2);

	KernelShutdown();

	TestSummary();
}

