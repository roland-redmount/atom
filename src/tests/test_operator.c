
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/operator.h"
#include "kernel/kernel.h"
#include "library/library.h"
#include "library/list.h"
#include "kernel/multiset.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "library/string.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
#include "lang/formula.h"
#include "lang/TermForm.h"
#include "parser/PredicateBuilder.h"
#include "testing/fixtures.h"
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
		(Atom[]) {GetListPredicateForm(), (Atom) {0}, (Atom) {0}},
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
 * Create a PERMUTE operator reordering the relation (list position element)
 * to (list element position) 
 */
static Operator * createPermutedListOperator(void)
{
	// The operator (list <ID position >INT element >LETTER)
	Operator * listOperator = GetListOperator(AT_LETTER);
	index8 argumentMap[3];
	ListSetByteArray(
		(index8[]) {0, 2, 1},		// (l p e) -> (l e p)
		argumentMap
	);
	return CreatePermuteOperator(
		3,
		0, 0, 0,	// no constants
		argumentMap, listOperator
	);
}


void testPermuteOperator(void)
{
	Operator * permuteOperator = createPermutedListOperator();

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
	CheckOperator(permuteOperator);
}


/**
 * This tests using a PROJECT operator to marginalize the relation
 * (list element position) to (list element), dropping the trailing
 * position argument. Dropping it leaves duplicate tuples, which PROJECT
 * removes, so that the result is again a valid relation.
 */
void testProjectOperator(void)
{
	Operator * permuteOperator = createPermutedListOperator();
	Operator * projectOperator = CreateProjectOperator(permuteOperator, 2, (index8[]) {0, 1});

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
	CheckOperator(projectOperator);
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
	Atom arguments[3] = {GetListPredicateForm(), (Atom) {0}, (Atom) {0}};
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
	CheckOperator(joinOperator);
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
	Operator * listIdOperator = GetListOperator(AT_ID);
	Operator * listLetterOperator = GetListOperator(AT_LETTER);

	index8 leftArgumentMap[3];
	ListSetByteArray(
		(index8[]) {0, 1, 2},		// (l p s) -> (l p s q e)
		leftArgumentMap
	);
	index8 rightArgumentMap[3];
	ListSetByteArray(
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

	// NOTE: a join operator does not keep track of its argument types, but they could be
	// inferred recursively from its child operators, since a leaf operator must be a
	// machine operator with specific argument types.

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
			ASSERT_TRUE(arguments[i]._int != 0)
		nElements++;
	}
	ASSERT_INT32_EQUAL(nElements, 2 * 3)
	OperatorFreeContext(context);
	IFactRelease(stringList);
	CheckOperator(joinOperator);
}


/**
 * Test a FILTER operator over the (list <ID position >INT element >LETTER) service.
 * The service produces the element, and the filter keeps only the tuples whose element
 * is the letter the caller bound, which gives the positions of that letter in the string.
 * No service provides that IO pattern, which is what FILTER is for; see OPERATOR_FILTER.
 */
void testFilterOperator(void)
{
	Operator * listOperator = GetListOperator(AT_LETTER);
	index8 elementIndex = GetListRoleIndex()[LIST_ROLE_ELEMENT];
	index8 positionIndex = GetListRoleIndex()[LIST_ROLE_POSITION];

	Operator * filterOperator = CreateFilterOperator(
		listOperator, (index8[]) {elementIndex}, 1);

	// The letter 'a' of "alibaba" occurs at positions 1, 5 and 7
	int64 const expectedPositions[] = {1, 5, 7};
	Atom string = CreateStringFromCString("alibaba");
	Atom arguments[3];
	ListSetTuple(
		(Atom[]) {string, (Atom) {0}, GetAlphabetLetter('a')},
		arguments
	);
	OperatorContext * context = OperatorCreateContext(filterOperator, arguments);

	for(index8 i = 0; i < 3; i++) {
		ASSERT_TRUE(OperatorCall(context))
		ASSERT_INT64_EQUAL(arguments[positionIndex]._int, expectedPositions[i])
		ASSERT_CHAR_EQUAL(LetterToChar(arguments[elementIndex], LETTER_LOWERCASE), 'a')
	}
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	// A filter yields its tuples in the index order of its child
	ASSERT_NOT_NULL(filterOperator->indexOrder)
	for(index8 i = 0; i < 3; i++)
		ASSERT_UINT32_EQUAL(filterOperator->indexOrder[i], listOperator->indexOrder[i])

	IFactRelease(string);
	CheckOperator(filterOperator);
}


/**
 * Test a CONSTRAIN operator over the JOIN operator of testJoinOperator2().
 * Constraining the two position arguments of (l p s q e) to be equal gives the
 * letter at position p of the p'th string of a list of strings.
 */
void testConstrainOperator(void)
{
	Operator * listIdOperator = GetListOperator(AT_ID);
	Operator * listLetterOperator = GetListOperator(AT_LETTER);

	index8 leftArgumentMap[3];
	ListSetByteArray(
		(index8[]) {0, 1, 2},		// (l p s) -> (l p s q e)
		leftArgumentMap
	);
	index8 rightArgumentMap[3];
	ListSetByteArray(
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
	CheckOperator(joinOperator);

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
		ASSERT_UINT64_EQUAL(arguments[1]._int, i + 1)
		ASSERT_CHAR_EQUAL(LetterToChar(arguments[3], LETTER_LOWERCASE), expectedLetters[i])
	}
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	IFactRelease(stringList);
	CheckOperator(constrainOperator);
}


#define TEST_UNION_N_ELEMENTS 	(3 + 4)

void testUnionOperator(void)
{
	// The UNION operator (list @list1 position p element e) | (list @list2 position p element e)
	Operator * listOperator = GetListOperator(AT_LETTER);

	// use PERMUTE to bind the list role to a constant
	index8 argumentMap[3];
	ListSetByteArray(
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

	// Setup execution context
	Atom arguments[2] = {0};
	OperatorContext * context = OperatorCreateContext(unionOperator, arguments);
	// Call union operator
	uint32 expectedPositions[TEST_UNION_N_ELEMENTS] = {1, 1, 2, 2, 3, 3, 4};
	char expectedCharacters[TEST_UNION_N_ELEMENTS] = "bfaoorf";
	for(index32 i = 0; i < TEST_UNION_N_ELEMENTS; i++) {
		ASSERT_TRUE(OperatorCall(context))
		ASSERT_INT32_EQUAL(arguments[0]._int, expectedPositions[i])
		ASSERT_CHAR_EQUAL(
			LetterToChar(arguments[1], LETTER_LOWERCASE),
			expectedCharacters[i]
		)
	}
	ASSERT_FALSE(OperatorCall(context));
	OperatorFreeContext(context);
	CheckOperator(unionOperator);
}


/**
 * A tuple both child operators provide is yielded once, including when providing it
 * exhausts one of them. That tuple is then the lookahead of the operator still running,
 * and yielding it has to be what completes the duplicate, or it is yielded a second
 * time when the remaining operator is drained.
 *
 * Here the letters of "fo" are a prefix of those of "foo", so the shorter operator is
 * exhausted by the duplicate (2 'o').
 */
void testUnionDuplicateAtExhaustion(void)
{
	Operator * listOperator = GetListOperator(AT_LETTER);
	index8 argumentMap[3];
	ListSetByteArray(
		(index8[]) {2, 0, 1},		// (@list p s) -> (p s), the list is constant 0
		argumentMap
	);

	Atom shortString = CreateStringFromCString("fo");
	Operator * shortService = CreatePermuteOperator(
		2, (Atom[]) {shortString}, (byte[]) {AT_ID}, 1, argumentMap, listOperator);
	IFactRelease(shortString);

	Atom longString = CreateStringFromCString("foo");
	Operator * longService = CreatePermuteOperator(
		2, (Atom[]) {longString}, (byte[]) {AT_ID}, 1, argumentMap, listOperator);
	IFactRelease(longString);

	Operator * unionOperator = CreateUnionOperator(shortService, longService);

	Atom arguments[2] = {0};
	OperatorContext * context = OperatorCreateContext(unionOperator, arguments);
	uint32 expectedPositions[3] = {1, 2, 3};
	char expectedCharacters[3] = "foo";
	for(index32 i = 0; i < 3; i++) {
		ASSERT_TRUE(OperatorCall(context))
		ASSERT_INT32_EQUAL(arguments[0]._int, expectedPositions[i])
		ASSERT_CHAR_EQUAL(
			LetterToChar(arguments[1], LETTER_LOWERCASE),
			expectedCharacters[i]
		)
	}
	ASSERT_FALSE(OperatorCall(context));
	OperatorFreeContext(context);
	CheckOperator(unionOperator);
}


/**
 * Every operator determines its index order from that of its children; see contract in operator.h.
 * A relation stored in a particular column order propagates that order upwards through
 * the operators applied to it.
 */
void testIndexOrder(void)
{
	// A B-tree operator yields its tuples in the index column order of its relation
	Operator * listOperator = GetListOperator(AT_LETTER);
	RelationTable const * listRelation = GetListRelationTable(AT_LETTER);
	ASSERT_NOT_NULL(listOperator->indexOrder)
	for(index8 i = 0; i < 3; i++)
		ASSERT_UINT32_EQUAL(listOperator->indexOrder[i], listRelation->indexColumns[i])

	// The relation is stored in the kernel order (list position element), so reordering
	// it to (list element position) yields tuples ordered by argument 0 (the list),
	// then argument 2 (the position), then argument 1 (the element)
	Operator * permuteOperator = createPermutedListOperator();
	ASSERT_UINT32_EQUAL(permuteOperator->indexOrder[0], 0)
	ASSERT_UINT32_EQUAL(permuteOperator->indexOrder[1], 2)
	ASSERT_UINT32_EQUAL(permuteOperator->indexOrder[2], 1)

	// PROJECT sorts tuple using a B-tree, and always give the identity index order
	Operator * projectOperator = CreateProjectOperator(permuteOperator, 2, (index8[]) {0, 1});
	ASSERT_UINT32_EQUAL(projectOperator->indexOrder[0], 0)
	ASSERT_UINT32_EQUAL(projectOperator->indexOrder[1], 1)

	// A JOIN takes the left child's order, then the right child's minus the arguments
	// the two share. Mapping the left child (list position element) to the arguments
	// (2 0 1) and the right child to (2 3 4), they share argument 2, which the left
	// child orders first.
	index8 leftMap[3];
	ListSetByteArray((index8[]) {2, 0, 1}, leftMap);
	index8 rightMap[3];
	ListSetByteArray((index8[]) {2, 3, 4}, rightMap);
	Operator * joinOperator = CreateJoinOperator(
		5, listOperator, leftMap, GetListOperator(AT_ID), rightMap);
	index8 expectedJoinOrder[5] = {2, 0, 1, 3, 4};
	for(index8 i = 0; i < 5; i++)
		ASSERT_UINT32_EQUAL(joinOperator->indexOrder[i], expectedJoinOrder[i])

	// An operator yielding at most one tuple has no index order
	MachineOperatorProvider singleTupleProvider = {
		.setupContext = 0,
		.call = 0,
		.finalizeContext = 0,
		.finalizeOperator = 0
	};
	Operator * singleTupleOperator = CreateMachineOperator(2, 0, &singleTupleProvider, 0, 0);
	ASSERT_NULL(singleTupleOperator->indexOrder)

	// A PERMUTE of an operator with no index order does not have an index order either
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

	// Drop all operators
	CheckOperator(joinOperator);
	CheckOperator(unionOperator);
	CheckOperator(reversedUnionOperator);
}


/**
 * A directed graph (prec:ID succ:ID) holding the cycle a -> b -> c -> a, together with
 * the path d -> e -> f, which is disconnected from it. The tuples are stored ordered by
 * the prec column, so that the relation can be looked up on it.
 */
#define TEST_N_EDGES	5

// Tuples in the transitive closure of each component
#define TEST_N_CYCLE_CLOSURE	(3 * 3)
#define TEST_N_PATH_CLOSURE		3

static RelationFixture graphFixture;


static void setupGraphFixture(void)
{
	SetupRelationFixture(&graphFixture, (char const * []) {"prec", "succ"}, 2);

	char const * precNames[TEST_N_EDGES] = {"a", "b", "c", "d", "e"};
	char const * succNames[TEST_N_EDGES] = {"b", "c", "a", "e", "f"};
	for(index8 i = 0; i < TEST_N_EDGES; i++)
		RelationFixtureAddTuple(
			&graphFixture, (char const * []) {precNames[i], succNames[i]});
}


/**
 * Create a FIXPOINT operator evaluating (before x after y) defined by the rules
 *
 *  (1) before x after y  <-  succ y prec x
 *  (2) before x after y  <-  succ z prec x & before z after y
 * 
 * Here x is a constant if nInputs > 0; and both x, y are constants if nInputs > 1.
 * The graph relation is written here in its canonical role order (succ prec), so that
 * argument 0 is the succ role and argument 1 the prec role.
 *
 * The (before after) relation is a FIXPOINT/2 operator with argument order (x y).
 * With inputs = 1, this yieds the operator tree
 *
 *	FIXPOINT/2[0 1]<0>(                      // derives (before after), x bound
 *		UNION/2[0 1](
 *			PERMUTE/2[0 1](1 0 {}            // rule (1), reordering (y x) to (x y)
 *				MACHINE/2[1 0])              // (succ y prec x), looked up on x
 *			PROJECT/2[0 1](0 1               // rule (2), dropping the shared z
 *				JOIN/3[0 2 1](               // join arguments are (x y z)
 *					2 0 MACHINE/2[1 0]       // (succ z prec x), looked up on x
 *					2 1 RECURSE/2[0 1]<0>))))// (before z after y), z bound
 *
 * The MACHINE operators yield the graph ordered by argument 1 (prec) before argument 0 (succ),
 * and rule (1) maps their argument 0 (y) to query argument 1 and their argument 1 (x)
 * to query argument 0.
 *
 * Both MACHINE operators are one and the same service. Their argument maps differ only
 * because rule (2) takes the succ role into the shared z rather than into y, which is
 * also why rule (1) needs a PERMUTE where rule (2) folds the reordering into the join.
 *
 * Called with nothing bound, the tree is the same except that the FIXPOINT operator binds
 * no argument and the graph relation is enumerated rather than looked up. The RECURSE
 * operator still binds z, as the join provides it either way.
 *
 * The caller obtains a reference to the operator.
 */
static Operator * createClosureOperator(index8 const inputArguments[], size8 nInputs)
{
	index8 precIndex = RelationFixtureRoleIndex(&graphFixture, "prec");
	index8 succIndex = RelationFixtureRoleIndex(&graphFixture, "succ");

	// Both rule bodies take the graph relation looked up on x when the caller bound it,
	// which is what confines the derivation to the reachable part of the graph, and
	// enumerate every edge when it did not. This is the choice dispatch makes for a
	// compiled rule, from the parameters the query leaves bound.
	byte parameterIO[2];
	parameterIO[precIndex] = nInputs ? PARAMETER_IN : PARAMETER_OUT;
	parameterIO[succIndex] = PARAMETER_OUT;
	Operator * edgeOperator = FindService(
		graphFixture.table->relation, CreateIOSignature(parameterIO, 2));
	ASSERT_NOT_NULL(edgeOperator)

	// Rule (1), the graph relation itself, with the edge arguments taken into the
	// closure argument order (x y)
	index8 edgeToClosureMap[2];
	edgeToClosureMap[precIndex] = 0;
	edgeToClosureMap[succIndex] = 1;
	Operator * baseOperator = CreatePermuteOperator(
		2, 0, 0, 0, edgeToClosureMap, edgeOperator);

	// Rule (2). The join arguments are (x y z), so that (x y) agree with the closure
	// and the shared z comes last. The left child provides (x z) and the right child
	// (z y), so the right child takes its first argument as an input.
	index8 edgeToJoinMap[2];
	edgeToJoinMap[precIndex] = 0;
	edgeToJoinMap[succIndex] = 2;
	index8 recurseMap[2] = {2, 1};

	Operator * recurseOperator = CreateRecurseOperator(2, (index8[]) {0}, 1);
	Operator * joinOperator = CreateJoinOperator(
		3, edgeOperator, edgeToJoinMap, recurseOperator, recurseMap);

	// Drop the shared z, keeping the closure arguments
	Operator * projectOperator = CreateProjectOperator(joinOperator, 2, (index8[]) {0, 1});

	Operator * unionOperator = CreateUnionOperator(baseOperator, projectOperator);

	Operator * fixpointOperator = CreateFixpointOperator(
		unionOperator, inputArguments, nInputs);
	return fixpointOperator;
}


/**
 * Test that the FIXPOINT operator terminates on the cyclic graph of the fixture.
 * Because the graph contains cycles, a naive top-down evaluation would descend forever.
 * A caller binding nothing asks for the whole relation, which is the closure of both
 * components of the graph.
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
	ASSERT_UINT32_EQUAL(nTuples, TEST_N_CYCLE_CLOSURE + TEST_N_PATH_CLOSURE)
	OperatorFreeContext(context);

	CheckOperator(closureOperator);
	TeardownRelationFixture(&graphFixture);
}


/**
 * The derivation is driven by the bindings the query asks for, and not run over the
 * whole relation and filtered afterwards. Asking for the nodes after a reaches the
 * three nodes of its cycle, and derives nothing for the component a cannot reach.
 */
void testFixpointCallBinding(void)
{
	setupGraphFixture();
	Operator * closureOperator = createClosureOperator((index8[]) {0}, 1);

	Atom nodeA = CreateStringFromCString("a");
	Atom arguments[2] = {nodeA, (Atom) {0}};

	OperatorContext * context = OperatorCreateContext(closureOperator, arguments);
	size32 nTuples = 0;
	while(OperatorCall(context)) {
		// every tuple yielded keeps the argument the caller bound
		ASSERT_UINT64_EQUAL(arguments[0].hash, nodeA.hash)
		nTuples++;
	}
	ASSERT_UINT32_EQUAL(nTuples, 3)

	// The tuples yielded would be the same either way, so what distinguishes a derivation
	// driven by the call bindings is how much of the relation it had to derive: the
	// closure of the cycle a belongs to, and nothing of the component it cannot reach.
	ASSERT_UINT32_EQUAL(FixpointNDerivedTuples(context), TEST_N_CYCLE_CLOSURE)
	OperatorFreeContext(context);

	IFactRelease(nodeA);
	CheckOperator(closureOperator);
	TeardownRelationFixture(&graphFixture);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	LoadLibraries();

	ExecuteTest(testMachineOperator);
	ExecuteTest(testPermuteOperator);
	ExecuteTest(testProjectOperator);
	ExecuteTest(testJoinOperator1);
	ExecuteTest(testJoinOperator2);
	ExecuteTest(testConstrainOperator);
	ExecuteTest(testFilterOperator);
	ExecuteTest(testUnionOperator);
	ExecuteTest(testUnionDuplicateAtExhaustion);
	ExecuteTest(testIndexOrder);
	ExecuteTest(testFixpointOperator);
	ExecuteTest(testFixpointCallBinding);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}

