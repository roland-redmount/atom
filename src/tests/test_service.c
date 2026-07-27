
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
 * This tests using a PERMUTE service to marginalize the relation
 * (list position element) to (list element). This alone does not
 * remove duplicates and so does not provide a valid relation;
 * wrapping a DEDUPLICATE services around PERMUTE yields unique tuples.
 */
void testPermuteService(void)
{
	// The service (list <ID position >UINT element >LETTER)
	Service * listService = GetCoreService(SERVICE_LIST_LETTER);
	// Reorder (list l position p element e) to (list l element e)
	// by providing the variable p as a "constant"
	TypedTuple * constants = CreateTypedTupleFromArray((TypedAtom[]) {anonymousVariable}, 1);
	index8 argumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {1, 0, 2},		// (l p e) -> (l e)
		argumentMap
	);
	Service * permuteService = CreatePermuteService(2, constants, argumentMap, listService);
	FreeTypedTuple(constants);

	// Arguments tuple (@stringList _ ) for the marginalize service
	Atom string = CreateStringFromCString("alibaba");
	Atom arguments[2] = {string, (Atom) {0}};

	// Call the PERMUTE service
	// This enumerates all elements of the string ("alibaba")
	ServiceContext * context = ServiceCreateContext(permuteService, arguments);
	size32 nElements = 0;
	while(ServiceCall(context)) {
		// PrintChar(LetterToChar(arguments[1], LETTER_LOWERCASE));
		// PrintChar('\n');
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 7)
	ServiceFreeContext(context);
	
	// Create a DEDUPLICATE service from the PERMUTE service
	Service * deduplicateService = CreateDeduplicateService(permuteService);
	// Call the service
	// This should yield the unique letters, sorted ("abil")
	arguments[0] = string;
	arguments[1] = (Atom) {0};
	context = ServiceCreateContext(deduplicateService, arguments);
	nElements = 0;
	while(ServiceCall(context)) {
		// PrintChar(LetterToChar(arguments[1], LETTER_LOWERCASE));
		// PrintChar('\n');
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 4)
	ServiceFreeContext(context);
	
	IFactRelease(string);
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
	Service * leftService = GetCoreService(SERVICE_MULTISET_NAME);
	// Right service needs a PERMUTE since it takes 1 argument only
	Service * rightServiceChild = GetCoreService(SERVICE_PREDICATE_FORM);
	// TODO: is this mapping correct?
	Service * rightService = CreatePermuteService(3, 0, (index8[]) {3}, rightServiceChild);
	
	// Create the JOIN service
	Service * joinService = CreateJoinService(leftService, rightService);
	ReleaseService(rightService);

	// Evaluate with arguments (@list-form, _ , _)
	Atom arguments[3];
	CoreFormSetTuple(
		FORM_LIST_POSITION_ELEMENT,
		(Atom[]) {GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT), (Atom) {0}, (Atom) {0}},
		arguments
	);
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
	// This frees the child services
	ReleaseService(joinService);
}


/**
 * Test evaluating the JOIN service
 * (list l position p element s) & (list s position q element e)
 * with arguments (l p s q e)
 */
void testJoinService2(void)
{
	Service * listService = GetCoreService(SERVICE_LIST_LETTER);
	
	// The left and right child services call the same machine service,
	// but with different argument maps. The argument map determines the
	// join service argument order.
	index8 leftServiceArgumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {1, 2, 3},		// (l p s) -> (l p s q e)
		leftServiceArgumentMap
	);
	index8 rightServiceArgumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {3, 4, 5},		// (s q e) -> (l p s q e)
		rightServiceArgumentMap
	);

	Service * leftService = CreatePermuteService(5, 0, leftServiceArgumentMap, listService);
	Service * rightService = CreatePermuteService(5, 0, rightServiceArgumentMap, listService);

	// Create the join service
	Service * joinService = CreateJoinService(leftService, rightService);
	ReleaseService(leftService);
	ReleaseService(rightService);

	// Arguments tuple (@stringList _  _ _ _)
	Atom string1 = CreateStringFromCString("foo");
	Atom string2 = CreateStringFromCString("bar");
	Atom stringList = CreateListFromArray((Atom[]) {string1, string2}, AT_LETTER, 2);
	IFactRelease(string1);
	IFactRelease(string2);

	Atom arguments[5];
	arguments[0] = stringList;
	for(index8 i = 1; i < 5; i++)
		arguments[i] = (Atom) {0};

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
	IFactRelease(stringList);
	ReleaseService(joinService);
}


#define TEST_UNION_N_ELEMENTS 	(3 + 4)

void testUnionService(void)
{
	// The UNION service (list @list1 position p element e) | (list @list2 position p element e)
	Service * listService = GetCoreService(SERVICE_LIST_LETTER);

	// use PERMUTE to drop the list role
	TypedAtom string1 = CreateTypedAtom(AT_ID, CreateStringFromCString("foo"));
	TypedTuple * constants1 = CreateTypedTupleFromArray((TypedAtom[]) {string1}, 1);

	index8 argumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {0, 1, 2},		// (@list p s) -> (p s)
		argumentMap
	);

	Service * service1 = CreatePermuteService(2, constants1, argumentMap, listService);
	FreeTypedTuple(constants1);
	ReleaseTypedAtom(string1);

	TypedAtom string2 = CreateTypedAtom(AT_ID, CreateStringFromCString("barf"));
	TypedTuple * constants2 = CreateTypedTupleFromArray((TypedAtom[]) {string2}, 1);
	Service * service2 = CreatePermuteService(2, constants2, argumentMap, listService);
	FreeTypedTuple(constants2);
	ReleaseTypedAtom(string2);

	Service * unionService = CreateUnionService(service1, service2);
	ReleaseService(service1);
	ReleaseService(service2);

	// Setup execution context
	Atom arguments[2] = {0};
	ServiceContext * context = ServiceCreateContext(unionService, arguments);
	// Call union service
	uint32 expectedPositions[TEST_UNION_N_ELEMENTS] = {1, 1, 2, 2, 3, 3, 4};
	char expectedCharacters[TEST_UNION_N_ELEMENTS] = "bfaoorf";
	for(index32 i = 0; i < TEST_UNION_N_ELEMENTS; i++) {
		ASSERT_TRUE(ServiceCall(context))
		ASSERT_INT32_EQUAL(arguments[0]._uint, expectedPositions[i])
		ASSERT_CHAR_EQUAL(
			LetterToChar(arguments[1], LETTER_LOWERCASE),
			expectedCharacters[i]
		)
	}
	ASSERT_FALSE(ServiceCall(context));
	ServiceFreeContext(context);
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

