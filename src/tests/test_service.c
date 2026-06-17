
#include "kernel/dispatch.h"
#include "kernel/letter.h"
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
	// TODO
}

/**
 * This tests using a PERMUTE service to marginalize a relation
 * (position list element) to (list element). This alone does not
 * remove duplicates and so does not provide a valid relation;
 * wrapping a DEDUPLICATE services around PERMUTE yields unique tuples.
 */
void testPermuteService(void)
{
	// The service (position list element)
	ServiceRecord const * listServiceRecord = RegistryGetCoreServiceRecord(FORM_LIST_POSITION_ELEMENT);
	// Reorder (position _ list l element s) to (list l element s),
	// providing the variable _ as a "constant"
	TypedTuple * constants = CreateTypedTupleFromArray((TypedAtom[]) {anonymousVariable}, 1);
	Service * permuteService = CreatePermuteService(
		2, constants, (index8[]) {0, 1, 2}, listServiceRecord->service);
	FreeTypedTuple(constants);

	// Arguments tuple (@stringList _ )
	TypedAtom string = CreateTypedAtom(AT_ID, CreateStringFromCString("alibaba"));
	TypedTuple * arguments = CreateTypedTupleFromArray(
		(TypedAtom[]) {string, anonymousVariable},
		2
	);

	// Call the PERMUTE service
	// This enumerates all elements of the string ("alibaba")
	ServiceContext * context = ServiceCreateContext(permuteService, arguments);
	size32 nElements = 0;
	while(ServiceCall(context)) {
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 7)
	ServiceFreeContext(context);
	FreeTypedTuple(arguments);
	
	// Create a DEDUPLICATE service from the PERMUTE service
	Service * deduplicateService = CreateDeduplicateService(permuteService);
	// Call the service
	// This yields the unique letters only ("abil")
	arguments = CreateTypedTupleFromArray((TypedAtom[]) {string, anonymousVariable},	2);
	context = ServiceCreateContext(deduplicateService, arguments);
	nElements = 0;
	while(ServiceCall(context)) {
		// TypedTuplePrint(arguments);
		// PrintChar('\n');
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 4)
	ServiceFreeContext(context);
	FreeTypedTuple(arguments);

	ReleaseTypedAtom(string);
	ReleaseService(deduplicateService);
	ReleaseService(permuteService);
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
	TypedTuple * arguments = CreateTypedTupleFromArray(
		(TypedAtom[]) {
			anonymousVariable,
			anonymousVariable,
			CreateTypedAtom(AT_ID, GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT)),
		},
		3
	);
	// TypedTuplePrint(arguments);
	// PrintChar('\n');
	// Setup execution context
	ServiceContext * context = ServiceCreateContext(joinService, arguments);

	// Call the service.
	// This should yield 3 tuples corresponding to the 3 roles of (list position element),
 	// since the right child service (predicate-form @multiset-form) matches a single tuple.
	size32 nElements = 0;
	while(ServiceCall(context)) {
		// TypedTuplePrint(arguments);
		// PrintChar('\n');
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 3);
	ServiceFreeContext(context);
	FreeTypedTuple(arguments);
	// This frees the child services
	ReleaseService(joinService);
}


/**
 * Test evaluating the JOIN service
 * (position p list l element s) & (position q list s element e)
 * with arguments (l p s q e)
 */
void testJoinService2(void)
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
	ReleaseTypedAtom(string1);
	ReleaseTypedAtom(string2);

	TypedTuple * arguments = CreateTypedTuple(5);
	TypedTupleSetElement(arguments, 0, stringList);
	for(index8 i = 1; i < 5; i++)
		TypedTupleSetElement(arguments, i, anonymousVariable);
	// TypedTuplePrint(arguments);
	// PrintChar('\n');

	// Setup execution context
	ServiceContext * context = ServiceCreateContext(joinService, arguments);
	// Call the join service
	size32 nElements = 0;
	while(ServiceCall(context)) {
		// TypedTuplePrint(arguments);
		// PrintChar('\n');
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 2 * 3)
	ServiceFreeContext(context);
	FreeTypedTuple(arguments);
	ReleaseTypedAtom(stringList);
	ReleaseService(joinService);
}


#define TEST_UNION_N_ELEMENTS 	(3 + 4)

void testUnionService(void)
{
	// The UNION service (position _ list @list1 element _) | (position _ list @list2 element -)
	ServiceRecord const * listServiceRecord = RegistryGetCoreServiceRecord(FORM_LIST_POSITION_ELEMENT);
	TypedAtom string1 = CreateTypedAtom(AT_ID, CreateStringFromCString("foo"));
	TypedTuple * constants1 = CreateTypedTupleFromArray((TypedAtom[]) {string1}, 1);
	Service * service1 = CreatePermuteService(
		2, constants1, (index8[]) {1, 0, 2}, listServiceRecord->service);
	FreeTypedTuple(constants1);
	ReleaseTypedAtom(string1);

	TypedAtom string2 = CreateTypedAtom(AT_ID, CreateStringFromCString("barf"));
	TypedTuple * constants2 = CreateTypedTupleFromArray((TypedAtom[]) {string2}, 1);
	Service * service2 = CreatePermuteService(
		2, constants2, (index8[]) {1, 0, 2}, listServiceRecord->service);
	FreeTypedTuple(constants2);
	ReleaseTypedAtom(string2);

	Service * unionService = CreateUnionService(service1, service2);
	ReleaseService(service1);
	ReleaseService(service2);

	TypedTuple * arguments = CreateTypedTupleFromArray(
		(TypedAtom[]) {anonymousVariable, anonymousVariable}, 2);

	// Setup execution context
	ServiceContext * context = ServiceCreateContext(unionService, arguments);
	uint32 expectedPositions[TEST_UNION_N_ELEMENTS] = {1, 1, 2, 2, 3, 3, 4};
	char expectedCharacters[TEST_UNION_N_ELEMENTS] = "bfaoorf";
	for(index32 i = 0; i < TEST_UNION_N_ELEMENTS; i++) {
		ASSERT_TRUE(ServiceCall(context))
		ASSERT_INT32_EQUAL(TypedTupleGetAtom(arguments, 0)._uint, expectedPositions[i])
		ASSERT_CHAR_EQUAL(
			LetterToChar(TypedTupleGetElement(arguments, 1), LETTER_LOWERCASE),
			expectedCharacters[i]
		)
		TypedTuplePrint(arguments);
		PrintChar('\n');
	}
	ASSERT_FALSE(ServiceCall(context));
	ServiceFreeContext(context);
	FreeTypedTuple(arguments);
	ReleaseService(unionService);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testPermuteService);
	ExecuteTest(testJoinService1);
	ExecuteTest(testJoinService2);
	ExecuteTest(testUnionService);

	KernelShutdown();

	TestSummary();
}

