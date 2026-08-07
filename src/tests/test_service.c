
#include "kernel/dispatch.h"
#include "kernel/letter.h"
#include "kernel/service.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/string.h"
#include "kernel/tuple.h"
#include "lang/Formula.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"


void testMachineService(void)
{
	// Test calling the B-tree service
	// (multiset @list-predicate-form element _ position _)
	Service * service = GetCoreService(SERVICE_MULTISET_NAME);
	ASSERT(service)
	ASSERT(service->type == SERVICE_MACHINE)

	Atom arguments[3];
	CoreFormSetTuple(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(Atom[]) {GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT), (Atom) {0}, (Atom) {0}},
		arguments
	);
	void * context = ServiceCreateContext(service, arguments);

	// this should yield 3 elements corresponding to the 3 roles of (list position element)
	size32 nElements = 0;
	while(ServiceCall(context))
		nElements++;
	ASSERT_INT32_EQUAL(nElements, 3);

	ServiceFreeContext(context);
}


/**
 * Reorder the relation (list position element) to (list element position).
 * A PERMUTE service keeps every argument of its child service, so its
 * tuples remain unique and it provides a valid relation.
 */
static Service * createReorderedListService(void)
{
	// The service (list <ID position >UINT element >LETTER)
	Service * listService = GetCoreService(SERVICE_LIST_LETTER);
	index8 argumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {0, 2, 1},		// (l p e) -> (l e p)
		argumentMap
	);
	return CreatePermuteService(3, 0, 0, 0, argumentMap, listService);
}


void testPermuteService(void)
{
	Service * permuteService = createReorderedListService();

	// Arguments tuple (@stringList _ _) for the reordered service
	Atom string = CreateStringFromCString("alibaba");
	Atom arguments[3] = {string, (Atom) {0}, (Atom) {0}};

	// Call the PERMUTE service.
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

	IFactRelease(string);
	ReleaseService(permuteService);
}


/**
 * This tests using a PROJECT service to marginalize the relation
 * (list element position) to (list element), dropping the trailing
 * position argument. Dropping it leaves duplicate tuples, which PROJECT
 * removes, so that the result is again a valid relation.
 */
void testProjectService(void)
{
	Service * permuteService = createReorderedListService();
	Service * projectService = CreateProjectService(permuteService, 2);
	ReleaseService(permuteService);

	// Arguments tuple (@stringList _) for the marginalize service
	Atom string = CreateStringFromCString("alibaba");
	Atom arguments[2] = {string, (Atom) {0}};

	// Call the service.
	// This should yield the unique letters, sorted ("abil")
	ServiceContext * context = ServiceCreateContext(projectService, arguments);
	char uniqueLetters[] = "abil";
	for(index8 i = 0; i < 4; i++) {
		ASSERT_TRUE(ServiceCall(context))
		ASSERT_CHAR_EQUAL(LetterToChar(arguments[1], LETTER_LOWERCASE), uniqueLetters[i])
	}
	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);

	IFactRelease(string);
	ReleaseService(projectService);
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
	// The right child service (predicate-form p) provides only the third argument
	// TODO: is this mapping correct?
	Service * rightService = GetCoreService(SERVICE_PREDICATE_FORM);

	// Create the JOIN service
	Service * joinService = CreateJoinService(
		3,
		leftService, (index8[]) {0, 1, 2},
		rightService, (index8[]) {2}
	);

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
		(index8[]) {0, 1, 2},		// (l p s) -> (l p s q e)
		leftServiceArgumentMap
	);
	index8 rightServiceArgumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {2, 3, 4},		// (s q e) -> (l p s q e)
		rightServiceArgumentMap
	);

	// Create the join service. The two child services are used directly:
	// the join service places their arguments into its own arguments tuple.
	Service * joinService = CreateJoinService(
		5,
		listIdService, leftServiceArgumentMap,
		listLetterService, rightServiceArgumentMap
	);

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
		joinArgumentTypes[leftServiceArgumentMap[i]] = leftArgumentTypes[i];
	for(index8 i = 0; i < 3; i++)
		joinArgumentTypes[rightServiceArgumentMap[i]] = rightArgumentTypes[i];

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
		// The two child services together provide every argument of the join service,
		// so no argument is left at the zero atom we started out with
		for(index8 i = 0; i < 5; i++)
			ASSERT_TRUE(arguments[i]._uint != 0)
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 2 * 3)
	ServiceFreeContext(context);
	IFactRelease(stringList);
	ReleaseService(joinService);
}


/**
 * Test a CONSTRAIN service over the JOIN service of testJoinService2().
 * Constraining the two position arguments of (l p s q e) to be equal gives the
 * letter at position p of the p'th string of a list of strings.
 */
void testConstrainService(void)
{
	Service * listIdService = GetCoreService(SERVICE_LIST_ID);
	Service * listLetterService = GetCoreService(SERVICE_LIST_LETTER);

	index8 leftServiceArgumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {0, 1, 2},		// (l p s) -> (l p s q e)
		leftServiceArgumentMap
	);
	index8 rightServiceArgumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {2, 3, 4},		// (s q e) -> (l p s q e)
		rightServiceArgumentMap
	);
	Service * joinService = CreateJoinService(
		5,
		listIdService, leftServiceArgumentMap,
		listLetterService, rightServiceArgumentMap
	);
	// Both position arguments take argument 1, so only tuples where they are equal
	// are yielded: (l p s q e) -> (l p s e)
	Service * constrainService = CreateConstrainService(
		4, (index8[]) {0, 1, 2, 1, 3}, joinService);
	ReleaseService(joinService);

	// test case, a list of two strings (two lists of letters)
	Atom string1 = CreateStringFromCString("foo");
	Atom string2 = CreateStringFromCString("bar");
	Atom stringList = CreateListFromArray((Atom[]) {string1, string2}, AT_ID, 2);
	IFactRelease(string1);
	IFactRelease(string2);

	// Arguments tuple (@stringList _ _ _)
	Atom arguments[4] = {stringList, (Atom) {0}, (Atom) {0}, (Atom) {0}};
	ServiceContext * context = ServiceCreateContext(constrainService, arguments);

	// The join service yields 2 * 3 tuples, of which we expect the two with p == q:
	// the 1st letter of "foo" and the 2nd letter of "bar"
	char expectedLetters[] = "fa";
	for(index8 i = 0; i < 2; i++) {
		ASSERT_TRUE(ServiceCall(context))
		ASSERT_UINT64_EQUAL(arguments[1]._uint, i + 1)
		ASSERT_CHAR_EQUAL(LetterToChar(arguments[3], LETTER_LOWERCASE), expectedLetters[i])
	}
	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);

	IFactRelease(stringList);
	ReleaseService(constrainService);
}


#define TEST_UNION_N_ELEMENTS 	(3 + 4)

void testUnionService(void)
{
	// The UNION service (list @list1 position p element e) | (list @list2 position p element e)
	Service * listService = GetCoreService(SERVICE_LIST_LETTER);

	// use PERMUTE to bind the list role to a constant
	index8 argumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {2, 0, 1},		// (@list p s) -> (p s), the list is constant 0
		argumentMap
	);

	Atom string1 = CreateStringFromCString("foo");
	Service * service1 = CreatePermuteService(
		2, (Atom[]) {string1}, (byte[]) {AT_ID}, 1, argumentMap, listService);
	IFactRelease(string1);

	Atom string2 = CreateStringFromCString("barf");
	Service * service2 = CreatePermuteService(
		2, (Atom[]) {string2}, (byte[]) {AT_ID}, 1, argumentMap, listService);
	IFactRelease(string2);

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

	ExecuteTest(testMachineService);
	ExecuteTest(testPermuteService);
	ExecuteTest(testProjectService);
	ExecuteTest(testJoinService1);
	ExecuteTest(testJoinService2);
	ExecuteTest(testConstrainService);
	ExecuteTest(testUnionService);

	KernelShutdown();

	TestSummary();
}

