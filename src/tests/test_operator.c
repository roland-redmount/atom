
#include "kernel/dispatch.h"
#include "kernel/letter.h"
#include "kernel/operator.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/string.h"
#include "kernel/tuple.h"
#include "lang/Formula.h"
#include "parser/PredicateBuilder.h"
#include "testing/testing.h"


void testMachineOperator(void)
{
	// Test calling the B-tree operator
	// (multiset @list-predicate-form element _ position _)
	Operator * op = GetCoreOperator(SERVICE_MULTISET_NAME);
	ASSERT(op)
	ASSERT(op->type == OPERATOR_MACHINE)

	Atom arguments[3];
	CoreFormSetTuple(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(Atom[]) {GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT), (Atom) {0}, (Atom) {0}},
		arguments
	);
	void * context = OperatorCreateContext(op, arguments);

	// this should yield 3 elements corresponding to the 3 roles of (list position element)
	size32 nElements = 0;
	while(OperatorCall(context))
		nElements++;
	ASSERT_INT32_EQUAL(nElements, 3);

	OperatorFreeContext(context);
}


/**
 * Reorder the relation (list position element) to (list element position).
 * A PERMUTE operator keeps every argument of its child operator, so its
 * tuples remain unique and it provides a valid relation.
 */
static Operator * createReorderedListOperator(void)
{
	// The operator (list <ID position >UINT element >LETTER)
	Operator * listOperator = GetCoreOperator(SERVICE_LIST_LETTER);
	index8 argumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {0, 2, 1},		// (l p e) -> (l e p)
		argumentMap
	);
	return CreatePermuteOperator(3, 0, 0, 0, argumentMap, listOperator);
}


void testPermuteOperator(void)
{
	Operator * permuteOperator = createReorderedListOperator();

	// Arguments tuple (@stringList _ _) for the reordered operator
	Atom string = CreateStringFromCString("alibaba");
	Atom arguments[3] = {string, (Atom) {0}, (Atom) {0}};

	// Call the PERMUTE operator.
	// This enumerates all elements of the string ("alibaba")
	OperatorContext * context = OperatorCreateContext(permuteOperator, arguments);
	size32 nElements = 0;
	while(OperatorCall(context)) {
		// PrintChar(LetterToChar(arguments[1], LETTER_LOWERCASE));
		// PrintChar('\n');
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 7)
	OperatorFreeContext(context);

	IFactRelease(string);
	ReleaseOperator(permuteOperator);
}


/**
 * This tests using a PROJECT operator to marginalize the relation
 * (list element position) to (list element), dropping the trailing
 * position argument. Dropping it leaves duplicate tuples, which PROJECT
 * removes, so that the result is again a valid relation.
 */
void testProjectOperator(void)
{
	Operator * permuteOperator = createReorderedListOperator();
	Operator * projectOperator = CreateProjectOperator(permuteOperator, 2);
	ReleaseOperator(permuteOperator);

	// Arguments tuple (@stringList _) for the marginalize operator
	Atom string = CreateStringFromCString("alibaba");
	Atom arguments[2] = {string, (Atom) {0}};

	// Call the operator.
	// This should yield the unique letters, sorted ("abil")
	OperatorContext * context = OperatorCreateContext(projectOperator, arguments);
	char uniqueLetters[] = "abil";
	for(index8 i = 0; i < 4; i++) {
		ASSERT_TRUE(OperatorCall(context))
		ASSERT_CHAR_EQUAL(LetterToChar(arguments[1], LETTER_LOWERCASE), uniqueLetters[i])
	}
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	IFactRelease(string);
	ReleaseOperator(projectOperator);
}


/**
 * Test creating and executing a JOIN operator
 * (multiset p element e multiple n) & (predicate-form p)
 * with parent arguments (p, e, n)
 */
void testJoinOperator1(void)
{
	// Left child operator (multiset p element e multiple n) from the registry
	Operator * leftOperator = GetCoreOperator(SERVICE_MULTISET_NAME);
	index8 leftArgumentMap[3];
	CoreFormSetByteArray(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(index8[]) {0, 1, 2},
		leftArgumentMap
	);
	// The right child operator (predicate-form p)
	Operator * rightOperator = GetCoreOperator(SERVICE_PREDICATE_FORM);
	index8 rightArgumentMap[1] = {0};

	// Create the JOIN operator
	Operator * joinOperator = CreateJoinOperator(
		3,
		leftOperator, leftArgumentMap,
		rightOperator, rightArgumentMap
	);

	// Evaluate with arguments (@list-form, _ , _)
	Atom arguments[3] = {GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT), (Atom) {0}, (Atom) {0}};
	// Setup execution context
	OperatorContext * context = OperatorCreateContext(joinOperator, arguments);

	// Call the operator.
	// This should yield 3 tuples corresponding to the 3 roles of (list position element),
 	// since the right child operator (predicate-form @multiset-form) matches a single tuple.
	size32 nElements = 0;
	while(OperatorCall(context)) {
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 3);
	OperatorFreeContext(context);
	// This frees the child operators
	ReleaseOperator(joinOperator);
}


/**
 * Test evaluating the JOIN operator
 * (list l position p element s) & (list s position q element e)
 * with arguments (l p s q e)
 */
void testJoinOperator2(void)
{
	// The left child enumerates the outer list (a list of strings, i.e. of ID
	// elements); the right child enumerates each string (a list of letters).
	// So they use different machine operators, with different argument maps.
	// The argument map determines the join operator argument order.
	Operator * listIdOperator = GetCoreOperator(SERVICE_LIST_ID);
	Operator * listLetterOperator = GetCoreOperator(SERVICE_LIST_LETTER);

	index8 leftArgumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {0, 1, 2},		// (l p s) -> (l p s q e)
		leftArgumentMap
	);
	index8 rightArgumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {2, 3, 4},		// (s q e) -> (l p s q e)
		rightArgumentMap
	);

	// Create the join operator. The two child operators are used directly:
	// the join operator places their arguments into its own arguments tuple.
	Operator * joinOperator = CreateJoinOperator(
		5,
		listIdOperator, leftArgumentMap,
		listLetterOperator, rightArgumentMap
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
	// (The created join operator does not keep track of its argument types,
	//  but they could be inferred recursively from its child operators, since
	//  a "leaf" operator must be a machine operator with specific argument types.)
	byte const * leftArgumentTypes = GetCoreRelationTable(RELATION_LIST_ID)->atomTypes;
	byte const * rightArgumentTypes = GetCoreRelationTable(RELATION_LIST_LETTER)->atomTypes;
	byte joinArgumentTypes[5];
	for(index8 i = 0; i < 3; i++)
		joinArgumentTypes[leftArgumentMap[i]] = leftArgumentTypes[i];
	for(index8 i = 0; i < 3; i++)
		joinArgumentTypes[rightArgumentMap[i]] = rightArgumentTypes[i];

	// Setup execution context
	OperatorContext * context = OperatorCreateContext(joinOperator, arguments);

	// Call the join operator.  We expect the tuples
	//  (@stringList 1 @string1 1 'f')
	//  (@stringList 1 @string1 2 'o')
	//  (@stringList 1 @string1 3 'o')
	//  (@stringList 2 @string2 1 'b')
	//  (@stringList 2 @string2 2 'a')
	//  (@stringList 2 @string2 3 '3')
	size32 nElements = 0;
	while(OperatorCall(context)) {
		// PrintTuple(joinArgumentTypes, arguments, 5);
		// PrintChar('\n');
		// The two child operators together provide every argument of the join operator,
		// so no argument is left at the zero atom we started out with
		for(index8 i = 0; i < 5; i++)
			ASSERT_TRUE(arguments[i]._uint != 0)
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 2 * 3)
	OperatorFreeContext(context);
	IFactRelease(stringList);
	ReleaseOperator(joinOperator);
}


/**
 * Test a CONSTRAIN operator over the JOIN operator of testJoinOperator2().
 * Constraining the two position arguments of (l p s q e) to be equal gives the
 * letter at position p of the p'th string of a list of strings.
 */
void testConstrainOperator(void)
{
	Operator * listIdOperator = GetCoreOperator(SERVICE_LIST_ID);
	Operator * listLetterOperator = GetCoreOperator(SERVICE_LIST_LETTER);

	index8 leftArgumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {0, 1, 2},		// (l p s) -> (l p s q e)
		leftArgumentMap
	);
	index8 rightArgumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {2, 3, 4},		// (s q e) -> (l p s q e)
		rightArgumentMap
	);
	Operator * joinOperator = CreateJoinOperator(
		5,
		listIdOperator, leftArgumentMap,
		listLetterOperator, rightArgumentMap
	);
	// Both position arguments take argument 1, so only tuples where they are equal
	// are yielded: (l p s q e) -> (l p s e)
	Operator * constrainOperator = CreateConstrainOperator(
		4, (index8[]) {0, 1, 2, 1, 3}, joinOperator);
	ReleaseOperator(joinOperator);

	// test case, a list of two strings (two lists of letters)
	Atom string1 = CreateStringFromCString("foo");
	Atom string2 = CreateStringFromCString("bar");
	Atom stringList = CreateListFromArray((Atom[]) {string1, string2}, AT_ID, 2);
	IFactRelease(string1);
	IFactRelease(string2);

	// Arguments tuple (@stringList _ _ _)
	Atom arguments[4] = {stringList, (Atom) {0}, (Atom) {0}, (Atom) {0}};
	OperatorContext * context = OperatorCreateContext(constrainOperator, arguments);

	// The join operator yields 2 * 3 tuples, of which we expect the two with p == q:
	// the 1st letter of "foo" and the 2nd letter of "bar"
	char expectedLetters[] = "fa";
	for(index8 i = 0; i < 2; i++) {
		ASSERT_TRUE(OperatorCall(context))
		ASSERT_UINT64_EQUAL(arguments[1]._uint, i + 1)
		ASSERT_CHAR_EQUAL(LetterToChar(arguments[3], LETTER_LOWERCASE), expectedLetters[i])
	}
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	IFactRelease(stringList);
	ReleaseOperator(constrainOperator);
}


#define TEST_UNION_N_ELEMENTS 	(3 + 4)

void testUnionOperator(void)
{
	// The UNION operator (list @list1 position p element e) | (list @list2 position p element e)
	Operator * listOperator = GetCoreOperator(SERVICE_LIST_LETTER);

	// use PERMUTE to bind the list role to a constant
	index8 argumentMap[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(index8[]) {2, 0, 1},		// (@list p s) -> (p s), the list is constant 0
		argumentMap
	);

	Atom string1 = CreateStringFromCString("foo");
	Operator * service1 = CreatePermuteOperator(
		2, (Atom[]) {string1}, (byte[]) {AT_ID}, 1, argumentMap, listOperator);
	IFactRelease(string1);

	Atom string2 = CreateStringFromCString("barf");
	Operator * service2 = CreatePermuteOperator(
		2, (Atom[]) {string2}, (byte[]) {AT_ID}, 1, argumentMap, listOperator);
	IFactRelease(string2);

	Operator * unionOperator = CreateUnionOperator(service1, service2);
	ReleaseOperator(service1);
	ReleaseOperator(service2);

	// Setup execution context
	Atom arguments[2] = {0};
	OperatorContext * context = OperatorCreateContext(unionOperator, arguments);
	// Call union operator
	uint32 expectedPositions[TEST_UNION_N_ELEMENTS] = {1, 1, 2, 2, 3, 3, 4};
	char expectedCharacters[TEST_UNION_N_ELEMENTS] = "bfaoorf";
	for(index32 i = 0; i < TEST_UNION_N_ELEMENTS; i++) {
		ASSERT_TRUE(OperatorCall(context))
		ASSERT_INT32_EQUAL(arguments[0]._uint, expectedPositions[i])
		ASSERT_CHAR_EQUAL(
			LetterToChar(arguments[1], LETTER_LOWERCASE),
			expectedCharacters[i]
		)
	}
	ASSERT_FALSE(OperatorCall(context));
	OperatorFreeContext(context);
	ReleaseOperator(unionOperator);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testMachineOperator);
	ExecuteTest(testPermuteOperator);
	ExecuteTest(testProjectOperator);
	ExecuteTest(testJoinOperator1);
	ExecuteTest(testJoinOperator2);
	ExecuteTest(testConstrainOperator);
	ExecuteTest(testUnionOperator);

	KernelShutdown();

	TestSummary();
}

