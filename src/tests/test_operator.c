
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/operator.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/multiset.h"
#include "kernel/Parameter.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
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
	Operator * projectOperator = CreateProjectOperator(permuteOperator, 2, (index8[]) {0, 1});
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


/**
 * Every operator declares the order in which it yields its tuples, deriving it from
 * its children rather than being told; see the ordering contract in operator.h.
 * A relation stored in a particular column order propagates that order upwards through
 * the operators applied to it.
 */
void testIndexOrder(void)
{
	// A B-tree operator yields its tuples in the index column order of its relation
	Operator * listOperator = GetCoreOperator(SERVICE_LIST_LETTER);
	RelationTable const * listRelation = GetCoreRelationTable(RELATION_LIST_LETTER);
	ASSERT_NOT_NULL(listOperator->indexOrder)
	for(index8 i = 0; i < 3; i++)
		ASSERT_UINT32_EQUAL(listOperator->indexOrder[i], listRelation->indexColumns[i])

	// The relation is stored in the kernel order (list position element), so reordering
	// it to (list element position) yields tuples ordered by argument 0 (the list),
	// then argument 2 (the position), then argument 1 (the element)
	Operator * permuteOperator = createReorderedListOperator();
	ASSERT_UINT32_EQUAL(permuteOperator->indexOrder[0], 0)
	ASSERT_UINT32_EQUAL(permuteOperator->indexOrder[1], 2)
	ASSERT_UINT32_EQUAL(permuteOperator->indexOrder[2], 1)

	// PROJECT materializes into a B-tree keyed on the arguments it keeps,
	// so it yields them in their own order
	Operator * projectOperator = CreateProjectOperator(permuteOperator, 2, (index8[]) {0, 1});
	ASSERT_UINT32_EQUAL(projectOperator->indexOrder[0], 0)
	ASSERT_UINT32_EQUAL(projectOperator->indexOrder[1], 1)

	// A JOIN takes the left child's order, then the right child's minus the arguments
	// the two share. Mapping the left child (list position element) to the arguments
	// (2 0 1) and the right child to (2 3 4), they share argument 2, which the left
	// child orders first.
	index8 leftMap[3];
	CoreFormSetByteArray(FORM_LIST_POSITION_ELEMENT, (index8[]) {2, 0, 1}, leftMap);
	index8 rightMap[3];
	CoreFormSetByteArray(FORM_LIST_POSITION_ELEMENT, (index8[]) {2, 3, 4}, rightMap);
	Operator * joinOperator = CreateJoinOperator(
		5, listOperator, leftMap, GetCoreOperator(SERVICE_LIST_ID), rightMap);
	index8 expectedJoinOrder[5] = {2, 0, 1, 3, 4};
	for(index8 i = 0; i < 5; i++)
		ASSERT_UINT32_EQUAL(joinOperator->indexOrder[i], expectedJoinOrder[i])

	// An operator yielding at most one tuple declares no order, as any order it named
	// would be arbitrary, and an operator relabeling its arguments declares none either
	MachineProvider singleTupleProvider = {
		.setupContext = 0,
		.call = 0,
		.finalizeContext = 0,
		.finalizeOperator = 0,
		.contextSize = 0
	};
	Operator * singleTupleOperator = CreateMachineOperator(2, 0, &singleTupleProvider, 0);
	ASSERT_NULL(singleTupleOperator->indexOrder)
	Operator * singleTuplePermute = CreatePermuteOperator(
		2, 0, 0, 0, (index8[]) {1, 0}, singleTupleOperator);
	ASSERT_NULL(singleTuplePermute->indexOrder)

	// Such an operator is ordered alike with any other, so a union takes the order
	// of its sibling whichever side it is on
	Operator * unionOperator = CreateUnionOperator(projectOperator, singleTuplePermute);
	ASSERT_UINT32_EQUAL(unionOperator->indexOrder[0], 0)
	ASSERT_UINT32_EQUAL(unionOperator->indexOrder[1], 1)
	Operator * reversedUnionOperator = CreateUnionOperator(singleTuplePermute, projectOperator);
	ASSERT_UINT32_EQUAL(reversedUnionOperator->indexOrder[0], 0)
	ASSERT_UINT32_EQUAL(reversedUnionOperator->indexOrder[1], 1)

	ReleaseOperator(reversedUnionOperator);
	ReleaseOperator(unionOperator);
	ReleaseOperator(singleTuplePermute);
	ReleaseOperator(singleTupleOperator);
	ReleaseOperator(joinOperator);
	ReleaseOperator(projectOperator);
	ReleaseOperator(permuteOperator);
}


/**
 * A directed graph (prec:ID succ:ID) holding the cycle a -> b -> c -> a, stored so
 * that the tuples are ordered by the prec column first.
 */
#define TEST_N_EDGES	3

static struct {
	Atom form;
	index8 precIndex;
	index8 succIndex;
	RelationTable const * table;
	TypedTuple * tuples[TEST_N_EDGES];
} graphFixture;


static void setupGraphFixture(void)
{
	Atom roles[2] = {
		CreateNameFromCString("prec"),
		CreateNameFromCString("succ")
	};
	graphFixture.form = CreatePredicateForm(roles, 2);
	graphFixture.precIndex = PredicateRoleIndex(graphFixture.form, roles[0]);
	graphFixture.succIndex = PredicateRoleIndex(graphFixture.form, roles[1]);
	for(index8 i = 0; i < 2; i++)
		NameRelease(roles[i]);

	graphFixture.table = CreateRelationBTreeWithServices(
		graphFixture.form, 2, (byte[]) {AT_ID, AT_ID}, (index8[]) {0, 1});

	char const * precNames[TEST_N_EDGES] = {"a", "b", "c"};
	char const * succNames[TEST_N_EDGES] = {"b", "c", "a"};
	for(index8 i = 0; i < TEST_N_EDGES; i++) {
		TypedAtom actors[2];
		actors[graphFixture.precIndex] =
			CreateTypedAtom(AT_ID, CreateStringFromCString(precNames[i]));
		actors[graphFixture.succIndex] =
			CreateTypedAtom(AT_ID, CreateStringFromCString(succNames[i]));
		graphFixture.tuples[i] = CreateTypedTupleFromArray(actors, 2);
		AssertFact(graphFixture.form, graphFixture.tuples[i], 0);
		for(index8 j = 0; j < 2; j++)
			ReleaseTypedAtom(actors[j]);
	}
}


static void teardownGraphFixture(void)
{
	for(index8 i = 0; i < TEST_N_EDGES; i++) {
		RetractFact(graphFixture.form, graphFixture.tuples[i]);
		FreeTypedTuple(graphFixture.tuples[i]);
	}
	ServiceRegistryRemoveAll(graphFixture.table);
	RelationRegistryRemove(graphFixture.table);
	IFactRelease(graphFixture.form);
}


/**
 * Build the transitive closure of the graph fixture, as the operator tree
 *
 *   FIXPOINT(UNION(edges, PROJECT(JOIN(edges, RECURSE))))
 *
 * which is the rule pair
 *
 *   closure x to y  <-  prec x succ y
 *   closure x to y  <-  prec x succ z & closure z to y
 *
 * The closure relation takes the argument layout of the graph relation, so that the
 * two branches of the union agree. The caller obtains a reference to the operator.
 */
static Operator * createClosureOperator(index8 const * inputArguments, size8 nInputs)
{
	index8 precIndex = graphFixture.precIndex;
	index8 succIndex = graphFixture.succIndex;

	// The graph relation with both arguments output, enumerating every edge
	Operator * edgeOperator = ServiceRegistryFind(
		graphFixture.table, (byte[]) {PARAMETER_OUT, PARAMETER_OUT});
	ASSERT_NOT_NULL(edgeOperator)

	// The join arguments are (x y z), with x and y laid out as the closure relation
	// and the shared z last. The left child provides (x z) and the right child (z y),
	// so the right child takes z as an input.
	index8 edgeMap[2];
	edgeMap[precIndex] = precIndex;
	edgeMap[succIndex] = 2;
	index8 recurseMap[2];
	recurseMap[precIndex] = 2;
	recurseMap[succIndex] = succIndex;

	Operator * recurseOperator = CreateRecurseOperator(2, (index8[]) {precIndex}, 1);
	Operator * joinOperator = CreateJoinOperator(
		3, edgeOperator, edgeMap, recurseOperator, recurseMap);
	ReleaseOperator(recurseOperator);

	// Drop the shared z, keeping the closure arguments
	Operator * projectOperator = CreateProjectOperator(joinOperator, 2, (index8[]) {0, 1});
	ReleaseOperator(joinOperator);

	Operator * unionOperator = CreateUnionOperator(edgeOperator, projectOperator);
	ReleaseOperator(projectOperator);

	Operator * fixpointOperator = CreateFixpointOperator(
		unionOperator, inputArguments, nInputs);
	ReleaseOperator(unionOperator);
	return fixpointOperator;
}


/**
 * A FIXPOINT operator derives a recursive relation by rounds, and terminates on the
 * cyclic graph of the fixture, where a top-down evaluation would descend forever.
 * The closure of a -> b -> c -> a is every pair of the three nodes.
 */
void testFixpointOperator(void)
{
	setupGraphFixture();
	Operator * closureOperator = createClosureOperator(0, 0);

	Atom arguments[2] = {(Atom) {0}, (Atom) {0}};
	OperatorContext * context = OperatorCreateContext(closureOperator, arguments);
	size32 nTuples = 0;
	while(OperatorCall(context))
		nTuples++;
	OperatorFreeContext(context);
	ASSERT_UINT32_EQUAL(nTuples, 3 * 3)

	ReleaseOperator(closureOperator);
	teardownGraphFixture();
}


/**
 * The arguments a caller binds restrict the tuples a FIXPOINT operator yields, and not
 * the ones it derives: asking for the nodes reachable from a gives all three nodes.
 */
void testFixpointInputArgument(void)
{
	setupGraphFixture();
	index8 precIndex = graphFixture.precIndex;
	Operator * closureOperator = createClosureOperator((index8[]) {precIndex}, 1);

	Atom nodeA = CreateStringFromCString("a");
	Atom arguments[2] = {(Atom) {0}, (Atom) {0}};
	arguments[precIndex] = nodeA;

	OperatorContext * context = OperatorCreateContext(closureOperator, arguments);
	size32 nTuples = 0;
	while(OperatorCall(context)) {
		// every tuple yielded keeps the argument the caller bound
		ASSERT_UINT64_EQUAL(arguments[precIndex].hash, nodeA.hash)
		nTuples++;
	}
	OperatorFreeContext(context);
	ASSERT_UINT32_EQUAL(nTuples, 3)

	IFactRelease(nodeA);
	ReleaseOperator(closureOperator);
	teardownGraphFixture();
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
	ExecuteTest(testIndexOrder);
	ExecuteTest(testFixpointOperator);
	ExecuteTest(testFixpointInputArgument);

	KernelShutdown();

	TestSummary();
}

