
#include "kernel/dispatch.h"
#include "kernel/letter.h"
#include "kernel/service.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "kernel/tuple.h"
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
 * (element e multiset m multiple n) & (predicate-form p)
 * with parent arguments (m, e, n)
 */
void testJoinService1(void)
{
	// Left child service (multiset m element e multiple n) from the registry
	Service * leftService = GetCoreService(SERVICE_MULTISET_NAME);
	// Right service is permuted (predicate m) -> (multiset m element _ multiple _)
	// where arguments _ are not filled in.
	// TODO: this is not really a valid service, as some values in the resulting tuples
	// are left undefined, This works as input for JOIN, but we should probably require
	// that every service yields a valid relation. Therefore, we should move this reindexing
	// into the JOIN service.
	Service * rightServiceChild = GetCoreService(SERVICE_PREDICATE_FORM);
	// TODO: is this mapping correct?
	Service * rightService = CreatePermuteService(3, 0, (index8[]) {3}, rightServiceChild);
	
	// Create the JOIN service
	Service * joinService = CreateJoinService(leftService, rightService);
	ReleaseService(rightService);

	// Evaluate with arguments (@list-form, _ , _)
	Atom arguments[3];
	CoreFormSetTuple(
		FORM_MULTISET_ELEMENT_MULTIPLE,
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
	// The left child enumerates the outer list (a list of strings, i.e. of ID
	// elements); the right child enumerates each string (a list of letters).
	// So they use different machine services, with different argument maps.
	// The argument map determines the join service argument order.
	Service * listIdService = GetCoreService(SERVICE_LIST_ID);
	Service * listLetterService = GetCoreService(SERVICE_LIST_LETTER);

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

	Service * leftService = CreatePermuteService(5, 0, leftServiceArgumentMap, listIdService);
	Service * rightService = CreatePermuteService(5, 0, rightServiceArgumentMap, listLetterService);

	// Create the join service
	Service * joinService = CreateJoinService(leftService, rightService);
	ReleaseService(leftService);
	ReleaseService(rightService);

	// test case, a list of two strings (two lists of letters)
	Atom string1 = CreateStringFromCString("foo");
	Atom string2 = CreateStringFromCString("bar");
	Atom stringList = CreateListFromArray((Atom[]) {string1, string2}, AT_ID, 2);
	IFactRelease(string1);
	IFactRelease(string2);

	// Arguments tuple (@stringList _  _ _ _)
	Atom arguments[5];
	arguments[0] = stringList;
	for(index8 i = 1; i < 5; i++)
		arguments[i] = (Atom) {0};

	// Create argument types for printing tuples.
	// (The created join service does not keep track of its argument types,
	//  but they could be inferred recursively from its child services, since
	//  a "leaf" service must be a MachineService with specific argument types.)
	byte const * leftArgumentTypes = GetCoreRelationTable(RELATION_LIST_ID)->atomTypes;
	byte const * rightArgumentTypes = GetCoreRelationTable(RELATION_LIST_LETTER)->atomTypes;
	byte joinArgumentTypes[5];
	for(index8 i = 0; i < 3; i++)
		joinArgumentTypes[leftServiceArgumentMap[i] - 1] = leftArgumentTypes[i];
	for(index8 i = 0; i < 3; i++)
		joinArgumentTypes[rightServiceArgumentMap[i] - 1] = rightArgumentTypes[i];

	// Setup execution context
	ServiceContext * context = ServiceCreateContext(joinService, arguments);

	// Call the join service.  We expect the tuples
	//  (@stringList 1 @string1 1 'f')
	//  (@stringList 1 @string1 2 'o')
	//  (@stringList 1 @string1 3 'o')
	//  (@stringList 2 @string2 1 'b')
	//  (@stringList 2 @string2 2 'a')
	//  (@stringList 2 @string2 3 '3')
	size32 nElements = 0;
	while(ServiceCall(context)) {
		// PrintTuple(joinArgumentTypes, arguments, 5);
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

