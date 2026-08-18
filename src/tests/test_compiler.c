
#include "kernel/compiler.h"
#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/list.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "kernel/tuple.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/MachineService.h"
#include "library/math.h"
#include "parser/ClauseBuilder.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


// Upper bound on the number of services a single query may compile to
#define MAX_COMPILED_SERVICES	4


void testCompilePermute1(void)
{
	// This rule compiles to a PERMUTE service with no constants
	// + z - x = y  <-  + x + y = z
	DictionaryEntry entry = DictionaryAddClauseFromCString("+ _z - _x = _y | ! + _x + _y = _z");
	Formula * queryTerm = CStringToTerm("+ 7 - 4 = _d");

	// This will yield a new service from the existing (+ + =) service
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// TODO: verify the compiled service atom types are correct

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 3);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom d = TermGetRoleActor(queryTerm->form, arguments, "=", 1);
	ASSERT_UINT64_EQUAL(d._uint, 3);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	ServiceRegistryRemove(service.relation, service.op);
	RelationRegistryRemove(service.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompilePermute2(void)
{
	// This rule compiles to a PERMUTE service with a constant 2.
	// The constant restricts an argument of the child service and cannot
	// introduce duplicate tuples, so no PROJECT service is needed.
	// number x addtwo y <- + x + 2 = y
	DictionaryEntry entry = DictionaryAddClauseFromCString("number _x addtwo _y | ! + _x + 2 = _y");
	Formula * queryTerm = CStringToTerm("number 3 addtwo _z");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom x = TermGetRoleActor(queryTerm->form, arguments, "number", 1);
	ASSERT_UINT64_EQUAL(x._uint, 3);

	Atom y = TermGetRoleActor(queryTerm->form, arguments, "addtwo", 1);
	ASSERT_UINT64_EQUAL(y._uint, 5);

	// Second call should fail (no more tuples)
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	ServiceRegistryRemove(service.relation, service.op);
	RelationRegistryRemove(service.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompileProject(void)
{
	// The variable p occurs in the clause but not in the query, so it obtains
	// an argument of its own, which is then dropped again by a PROJECT service:
	// the rule compiles to PROJECT(PERMUTE(...)).
	// set s element e <- list s position p element e
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"set _s element _e | ! list _s position _p element _e");
	Formula * queryTerm = CStringToTerm("set \"alibaba\" element _e");

	// The element role is an untyped output, so the term matches every
	// (list position element) relation: one per element type. We therefore
	// get one compiled service per element type, and must enumerate them all.
	// Only the LETTER-element service yields tuples, as "alibaba" is a string;
	// the ID-element service is registered but matches nothing.
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 2)

	// The unique letters of "alibaba"
	char uniqueLetters[4] = "abil";
	index8 elementRoleIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(queryTerm->form),
		CreateNameFromCString("element")
	);
	int k = 0;
	for(index8 i = 0; i < nServices; i++) {
		ASSERT_NOT_NULL(services[i].relation)
		ASSERT_NOT_NULL(services[i].op)

		Atom arguments[2];
		TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
		void * context = OperatorCreateContext(services[i].op, arguments);
		while(OperatorCall(context)) {
			char c = LetterToChar(arguments[elementRoleIndex], LETTER_LOWERCASE);
			ASSERT(k < 4)
			ASSERT_CHAR_EQUAL(c, uniqueLetters[k])
			k++;
		}
		OperatorFreeContext(context);
	}
	ASSERT_UINT32_EQUAL(k, 4);

	for(index8 i = 0; i < nServices; i++) {
		ServiceRegistryRemove(services[i].relation, services[i].op);
		RelationRegistryRemove(services[i].relation);
	}
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


/**
 * The head variable _n occurs in no body term, so no term of the conjunction
 * provides that argument. Such a rule cannot yield a valid relation, as that
 * argument would be left undefined, and so must be rejected.
 */
void testCompileUnconstrainedHeadVariable(void)
{
	// set s element e size n <- list s position p element e
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"set _s element _e size _n | ! list _s position _p element _e");
	Formula * queryTerm = CStringToTerm("set \"ab\" element _e size _z");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 0)

	for(index8 i = 0; i < nServices; i++) {
		ServiceRegistryRemove(services[i].relation, services[i].op);
		RelationRegistryRemove(services[i].relation);
	}
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompileJoin1(void)
{
	// This rule compiles to a JOIN service
	// first x second y third z  <-  + x + 1 = y & + y + 1 = z
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"first _x second _y third _z | ! + _x + 1 = _y | ! + _y + 1 = _z");
	Formula * queryTerm = CStringToTerm("first 3 second _s third _t");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 3);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom y = TermGetRoleActor(queryTerm->form, arguments, "second", 1);
	ASSERT_UINT64_EQUAL(y._uint, 4);

	Atom z = TermGetRoleActor(queryTerm->form, arguments, "third", 1);
	ASSERT_UINT64_EQUAL(z._uint, 5);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	ServiceRegistryRemove(service.relation, service.op);
	RelationRegistryRemove(service.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompileJoin2(void)
{
	// As testCompileJoin1, but the variable y linking the two terms does not
	// occur in the query. It obtains an argument of its own so that the JOIN
	// service can constrain the two terms against each other, and that argument
	// is dropped again by a PROJECT service:
	// PROJECT(JOIN(+ x + 1 = y, + y + 1 = z), 2)
	// first x third z  <-  + x + 1 = y & + y + 1 = z
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"first _x third _z | ! + _x + 1 = _y | ! + _y + 1 = _z");
	Formula * queryTerm = CStringToTerm("first 3 third _t");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom t = TermGetRoleActor(queryTerm->form, arguments, "third", 1);
	ASSERT_UINT64_EQUAL(t._uint, 5);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	ServiceRegistryRemove(service.relation, service.op);
	RelationRegistryRemove(service.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompileUnion(void)
{
	// Two rules resulting in a UNION service
	// number x neighbor y <- = y + x + 1     (y = x + 1)
	// number x neighbor y <- = x + y + 1     (x = y - 1 <-> y = x - 1)
	DictionaryEntry entry1 = DictionaryAddClauseFromCString(
		"number _x neighbor _y | ! = _y + _x + 1");
	DictionaryEntry entry2 = DictionaryAddClauseFromCString(
		"number _x neighbor _y | ! = _x + _y + 1");
	Formula * queryTerm = CStringToTerm("number 5 neighbor _y");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	// The atom types are encoded in the relation table associated with
	// the compiled service ...
	// PrintTuple(atomTypes?, arguments, 3);
	// PrintChar('\n');
	Atom y = TermGetRoleActor(queryTerm->form, arguments, "neighbor", 1);
	ASSERT_TRUE(y._uint == 4);

	ASSERT_TRUE(OperatorCall(context))
	// PrintTuple(arguments, 3);
	// PrintChar('\n');
	y = TermGetRoleActor(queryTerm->form, arguments, "neighbor", 1);
	ASSERT_TRUE(y._uint == 6);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	ServiceRegistryRemove(service.relation, service.op);
	RelationRegistryRemove(service.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry1);
	DictionaryRemoveClause(&entry2);
}


// A directed graph (edge:ID from:ID to:ID), two of whose edges are self edges
#define TEST_N_EDGES	4

static struct {
	Atom form;
	RelationTable const * table;
	TypedTuple * tuples[TEST_N_EDGES];
} edgeFixture;


static void setupEdgeFixture(void)
{
	Atom roles[3] = {
		CreateNameFromCString("edge"),
		CreateNameFromCString("from"),
		CreateNameFromCString("to")
	};
	Atom predicateForm = CreatePredicateForm(roles, 3);
	edgeFixture.form = CreateTermForm(predicateForm, true);
	index8 edgeIndex = PredicateRoleIndex(predicateForm, roles[0]);
	index8 fromIndex = PredicateRoleIndex(predicateForm, roles[1]);
	index8 toIndex = PredicateRoleIndex(predicateForm, roles[2]);
	IFactRelease(predicateForm);
	for(index8 i = 0; i < 3; i++)
		NameRelease(roles[i]);

	byte atomTypes[3] = {AT_ID, AT_ID, AT_ID};
	edgeFixture.table = CreateRelationBTreeWithServices(
		edgeFixture.form, 3, atomTypes, (index8[]) {0, 1, 2});

	// The graph a -> b, a -> a, b -> b, b -> c. Strings are lists of letters,
	// so the edges are named ep..es rather than e1..e4, and their letters are
	// kept clear of the node names.
	char const * edgeNames[TEST_N_EDGES] = {"ep", "eq", "er", "es"};
	char const * fromNames[TEST_N_EDGES] = {"a", "a", "b", "b"};
	char const * toNames[TEST_N_EDGES] = {"b", "a", "b", "c"};
	for(index8 i = 0; i < TEST_N_EDGES; i++) {
		TypedAtom actors[3];
		actors[edgeIndex] = CreateTypedAtom(AT_ID, CreateStringFromCString(edgeNames[i]));
		actors[fromIndex] = CreateTypedAtom(AT_ID, CreateStringFromCString(fromNames[i]));
		actors[toIndex] = CreateTypedAtom(AT_ID, CreateStringFromCString(toNames[i]));
		edgeFixture.tuples[i] = CreateTypedTupleFromArray(actors, 3);
		// the relation table now holds a reference to each atom
		AssertFact(edgeFixture.form, edgeFixture.tuples[i], 0);
		for(index8 j = 0; j < 3; j++)
			ReleaseTypedAtom(actors[j]);
	}
}


static void teardownEdgeFixture(void)
{
	// the relation table must be empty before it can be removed
	for(index8 i = 0; i < TEST_N_EDGES; i++) {
		RetractFact(edgeFixture.form, edgeFixture.tuples[i]);
		FreeTypedTuple(edgeFixture.tuples[i]);
	}
	ServiceRegistryRemoveAll(edgeFixture.table);
	RelationRegistryRemove(edgeFixture.table);
	IFactRelease(edgeFixture.form);
}


/**
 * The variable x occurs twice in the term (edge e from x to x), which constrains
 * the two arguments providing it to be equal: the rule asks for the nodes of the
 * graph that have a self edge. The term compiles to a CONSTRAIN service, and e,
 * which does not occur in the query, is dropped again by a PROJECT service.
 */
void testCompileConstrain(void)
{
	setupEdgeFixture();

	// self x <- edge e from x to x
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"self _x | ! edge _e from _x to _x");
	Formula * queryTerm = CStringToTerm("self _y");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)

	// Only a and b have a self edge. The tuples are sorted by atom, so we do not
	// know in which order they arrive.
	Atom nodeA = CreateStringFromCString("a");
	Atom nodeB = CreateStringFromCString("b");
	bool foundA = false;
	bool foundB = false;
	size32 nTuples = 0;

	Atom arguments[1] = {(Atom) {0}};
	void * context = OperatorCreateContext(services[0].op, arguments);
	while(OperatorCall(context)) {
		foundA = foundA || (arguments[0].hash == nodeA.hash);
		foundB = foundB || (arguments[0].hash == nodeB.hash);
		nTuples++;
	}
	OperatorFreeContext(context);

	ASSERT_UINT32_EQUAL(nTuples, 2)
	ASSERT_TRUE(foundA)
	ASSERT_TRUE(foundB)

	IFactRelease(nodeA);
	IFactRelease(nodeB);
	ServiceRegistryRemove(services[0].relation, services[0].op);
	RelationRegistryRemove(services[0].relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
	teardownEdgeFixture();
}


/**
 * Compile the query term (number 4 faculty f) under the recursive rule
 * 
 *  number n faculty f <- + m + 1 = n & number m faculty e & * e * n = f
 * 
 * with the fact (number 0 faculty 1) terminating the recursion.
 * This query is typical of recursive logical resolution a'la Prolog.
 */
void testCompileRecursiveJoin1(void)
{
	// The recursive rule
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"number _n faculty _f | ! + _m + 1 = _n | ! number _m faculty _e | ! * _e * _n = _f");
	// Create terminating fact, provide by a B-tree service
	Formula * terminatingFact = CStringToTerm("number 0 faculty 1");	
	RelationTable const * table = CreateRelationBTreeWithServices(
		terminatingFact->form, 2,
		TypedTuplePeekAtomTypes(terminatingFact->actors),
		(index8[]) {0, 1}
	);
	AssertFact(terminatingFact->form, terminatingFact->actors, 0);
	// Compile the query
	Formula * queryTerm = CStringToTerm("number 4 faculty _f");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 3);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom f = TermGetRoleActor(queryTerm->form, arguments, "faculty", 1);
	ASSERT_UINT64_EQUAL(f._uint, 24);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	ServiceRegistryRemove(service.relation, service.op);
	RelationRegistryRemove(service.relation);
	FreeFormula(queryTerm);
	RetractFact(terminatingFact->form, terminatingFact->actors);
	ServiceRegistryRemoveAll(table);
	RelationRegistryRemove(table);
	FreeFormula(terminatingFact);
	DictionaryRemoveClause(&entry);
}

/**
 * The relation (prec x succ y), x immediately preceding y, holding the graph
 *
 *   a -> b -> c -> d      with c -> b closing a cycle
 *   e -> f                a component nothing reaches from a
 *
 * The nodes are strings, and so are written quoted in a query: an actor must be a
 * literal, and a bare word is a role name to the parser. The relation is stored ordered
 * by the prec role, so that it can be looked up on it.
 */
#define TEST_N_PREC_SUCC_EDGES	5

// Tuples in the transitive closure of the whole graph: three from each of a, b and c,
// and the single edge of the other component
#define TEST_N_CLOSURE_TUPLES	10

static struct {
	Atom form;
	index8 precIndex;
	index8 succIndex;
	RelationTable const * table;
	TypedTuple * tuples[TEST_N_PREC_SUCC_EDGES];
} precSuccFixture;


static void setupPrecSuccFixture(void)
{
	Atom roles[2] = {
		CreateNameFromCString("prec"),
		CreateNameFromCString("succ")
	};
	Atom predicateForm = CreatePredicateForm(roles, 2);
	precSuccFixture.form = CreateTermForm(predicateForm, true);
	precSuccFixture.precIndex = PredicateRoleIndex(predicateForm, roles[0]);
	precSuccFixture.succIndex = PredicateRoleIndex(predicateForm, roles[1]);
	IFactRelease(predicateForm);
	for(index8 i = 0; i < 2; i++)
		NameRelease(roles[i]);

	precSuccFixture.table = CreateRelationBTreeWithServices(
		precSuccFixture.form, 2, (byte[]) {AT_ID, AT_ID},
		(index8[]) {precSuccFixture.precIndex, precSuccFixture.succIndex});

	char const * precNames[TEST_N_PREC_SUCC_EDGES] = {"a", "b", "c", "c", "e"};
	char const * succNames[TEST_N_PREC_SUCC_EDGES] = {"b", "c", "d", "b", "f"};
	for(index8 i = 0; i < TEST_N_PREC_SUCC_EDGES; i++) {
		TypedAtom actors[2];
		actors[precSuccFixture.precIndex] =
			CreateTypedAtom(AT_ID, CreateStringFromCString(precNames[i]));
		actors[precSuccFixture.succIndex] =
			CreateTypedAtom(AT_ID, CreateStringFromCString(succNames[i]));
		precSuccFixture.tuples[i] = CreateTypedTupleFromArray(actors, 2);
		AssertFact(precSuccFixture.form, precSuccFixture.tuples[i], 0);
		for(index8 j = 0; j < 2; j++)
			ReleaseTypedAtom(actors[j]);
	}
}


static void teardownPrecSuccFixture(void)
{
	for(index8 i = 0; i < TEST_N_PREC_SUCC_EDGES; i++) {
		RetractFact(precSuccFixture.form, precSuccFixture.tuples[i]);
		FreeTypedTuple(precSuccFixture.tuples[i]);
	}
	ServiceRegistryRemoveAll(precSuccFixture.table);
	RelationRegistryRemove(precSuccFixture.table);
	IFactRelease(precSuccFixture.form);
}


// The rules defining (before after) as the transitive closure of (prec succ)
static void addTransitiveClosureRules(DictionaryEntry * base, DictionaryEntry * recursive)
{
	*base = DictionaryAddClauseFromCString(
		"before _x after _y | ! prec _x succ _y");
	*recursive = DictionaryAddClauseFromCString(
		"before _x after _y | ! prec _x succ _z | ! before _z after _y");
}


/**
 * Compile the query term (before a after d) under the rules
 *
 *  before x after y <- prec x succ y
 *  before x after y <- prec x succ z & before z after y
 *
 * where the relation (prec x succ y) indicates x immediately preceding y. The second
 * rule is recursive, so the query compiles to a FIXPOINT operator deriving the relation.
 * This is a typical example of fixpoint semantics a'la Datalog, and the graph has a
 * cycle, which a top-down resolution a'la Prolog would descend forever.
 *
 * Both roles of the query are bound, so the compiled service answers whether d comes
 * after a, which it does through the path a -> b -> c -> d.
 */
void testCompileRecursiveJoin2(void)
{
	setupPrecSuccFixture();
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	addTransitiveClosureRules(&entry1, &entry2);

	Formula * queryTerm = CStringToTerm("before \"a\" after \"d\"");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service. Both arguments are bound, so it yields the query tuple itself
	// if the relation holds it, and nothing otherwise.
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom before = TermGetRoleActor(queryTerm->form, arguments, "before", 1);
	Atom after = TermGetRoleActor(queryTerm->form, arguments, "after", 1);
	Atom nodeA = CreateStringFromCString("a");
	Atom nodeD = CreateStringFromCString("d");
	ASSERT_UINT64_EQUAL(before.hash, nodeA.hash)
	ASSERT_UINT64_EQUAL(after.hash, nodeD.hash)
	IFactRelease(nodeA);
	IFactRelease(nodeD);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	ServiceRegistryRemove(service.relation, service.op);
	RelationRegistryRemove(service.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	teardownPrecSuccFixture();
}


/**
 * The same rules with the after role left free, asking for every node reaching d from a.
 * This is the query the derivation is driven by its call bindings for: the nodes after a
 * are b, c and d, and the component e -> f is never derived, as nothing calls for it.
 */
void testCompileRecursiveReachable(void)
{
	setupPrecSuccFixture();
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	addTransitiveClosureRules(&entry1, &entry2);

	Formula * queryTerm = CStringToTerm("before \"a\" after _y");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// The nodes after a, which the fixpoint yields in its own order
	char const * expectedNodes[3] = {"b", "c", "d"};
	bool found[3] = {false, false, false};

	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	size32 nTuples = 0;
	while(OperatorCall(context)) {
		Atom after = TermGetRoleActor(queryTerm->form, arguments, "after", 1);
		for(index8 i = 0; i < 3; i++) {
			Atom node = CreateStringFromCString(expectedNodes[i]);
			found[i] = found[i] || (after.hash == node.hash);
			IFactRelease(node);
		}
		nTuples++;
	}
	OperatorFreeContext(context);

	ASSERT_UINT32_EQUAL(nTuples, 3)
	for(index8 i = 0; i < 3; i++)
		ASSERT_TRUE(found[i])

	ServiceRegistryRemove(service.relation, service.op);
	RelationRegistryRemove(service.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	teardownPrecSuccFixture();
}


/**
 * The same rules with both roles left free, which asks for the whole relation: the
 * transitive closure of the entire graph, both of its components included.
 *
 * The closure holds (b b) and (c c), as b and c lie on a cycle and so come after
 * themselves, but not (a a), as no edge leads back to a.
 */
void testCompileRecursiveClosure(void)
{
	setupPrecSuccFixture();
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	addTransitiveClosureRules(&entry1, &entry2);

	Formula * queryTerm = CStringToTerm("before _x after _y");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	char const * expectedBefore[TEST_N_CLOSURE_TUPLES] = {
		"a", "a", "a", "b", "b", "b", "c", "c", "c", "e"};
	char const * expectedAfter[TEST_N_CLOSURE_TUPLES] = {
		"b", "c", "d", "b", "c", "d", "b", "c", "d", "f"};
	bool found[TEST_N_CLOSURE_TUPLES] = {false};

	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	size32 nTuples = 0;
	while(OperatorCall(context)) {
		Atom before = TermGetRoleActor(queryTerm->form, arguments, "before", 1);
		Atom after = TermGetRoleActor(queryTerm->form, arguments, "after", 1);
		for(index8 i = 0; i < TEST_N_CLOSURE_TUPLES; i++) {
			Atom expectedBeforeNode = CreateStringFromCString(expectedBefore[i]);
			Atom expectedAfterNode = CreateStringFromCString(expectedAfter[i]);
			if((before.hash == expectedBeforeNode.hash)
				&& (after.hash == expectedAfterNode.hash))
				found[i] = true;
			IFactRelease(expectedBeforeNode);
			IFactRelease(expectedAfterNode);
		}
		nTuples++;
	}
	OperatorFreeContext(context);

	ASSERT_UINT32_EQUAL(nTuples, TEST_N_CLOSURE_TUPLES)
	for(index8 i = 0; i < TEST_N_CLOSURE_TUPLES; i++)
		ASSERT_TRUE(found[i])

	ServiceRegistryRemove(service.relation, service.op);
	RelationRegistryRemove(service.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	teardownPrecSuccFixture();
}


/**
 * Compute the query (! even 3) against the rule
 * 
 *   ! even x | ! odd x
 * 
 * and the fact (odd 3). 
 */
void testCompileNegatedTerm(void)
{
	// Setup the fact (odd 3)
	Formula * odd3term = CStringToTerm("odd 3");
	RelationTable const *evenTable = CreateRelationBTreeWithServices(
		odd3term->form, 1, (byte[]) {AT_INT}, (index8[]) {0});
	AssertFact(odd3term->form, odd3term->actors, 0);
	// setup the rule
	DictionaryEntry entry = DictionaryAddClauseFromCString("! even _x | ! odd _x");
	Formula * queryTerm = CStringToTerm("! even 3");

	// compile the query
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the resulting service
	Atom arguments[1];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 1);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom x = TermGetRoleActor(queryTerm->form, arguments, "even", 1);
	ASSERT_UINT64_EQUAL(x._uint, 3);

	// Second call should fail (no more tuples)
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	// teardown
	ServiceRegistryRemove(service.relation, service.op);
	RelationRegistryRemove(service.relation);
	FreeFormula(queryTerm);
	
	DictionaryRemoveClause(&entry);

	RetractFact(odd3term->form, odd3term->actors);
	ServiceRegistryRemoveAll(evenTable);
	RelationRegistryRemove(evenTable);
	FreeFormula(odd3term);
}


/**
 * Compile the query (number n square s) against rule
 * 
 *   number n square s <- lower 1 number n upper 4 & * n * n = s
 * 
 * where (lower number upper) is the computed range relation, yielding the numbers
 * 1 to 4, and the JOIN evaluates the multiplication for each of them.
 */
void testCompileSquares(void)
{
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"number _n square _s | ! lower 1 number _n upper 4 | ! * _n * _n = _s");
	Formula * queryTerm = CStringToTerm("number _n square _s");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);

	for(int64 expected = 1; expected <= 4; expected++) {
		ASSERT_TRUE(OperatorCall(context))
		Atom n = TermGetRoleActor(queryTerm->form, arguments, "number", 1);
		ASSERT_INT64_EQUAL(n._int, expected)
		Atom s = TermGetRoleActor(queryTerm->form, arguments, "square", 1);
		ASSERT_INT64_EQUAL(s._int, expected * expected)
	}
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	ServiceRegistryRemove(service.relation, service.op);
	RelationRegistryRemove(service.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testCompilePermute1);
	ExecuteTest(testCompilePermute2);
	ExecuteTest(testCompileProject);
	ExecuteTest(testCompileJoin1);
	ExecuteTest(testCompileJoin2);
	ExecuteTest(testCompileUnion);
	ExecuteTest(testCompileUnconstrainedHeadVariable);
	ExecuteTest(testCompileConstrain);

	ExecuteTest(testCompileRecursiveJoin2);
	ExecuteTest(testCompileRecursiveReachable);
	ExecuteTest(testCompileRecursiveClosure);

	ExecuteTest(testCompileNegatedTerm);
	ExecuteTest(testCompileSquares);

	// TODO: compiling a recursive rule over an infinite domain. The relation has no
	// finite fixpoint and the call bindings n = 4, 3, 2, 1, 0, -1, -2, ... do not
	// terminate either, so this needs the precondition ? < n > 0: to guard the
	// recursive clause; see the notes on termination in compiler.md.
	// ExecuteTest(testCompileRecursiveJoin1);

	FreeMachineServices();
	TestSummary();
}
