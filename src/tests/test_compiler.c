
#include "kernel/compiler.h"
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "library/list.h"
#include "kernel/Relation.h"
#include "kernel/RelationTable.h"
#include "kernel/ServiceRegistry.h"
#include "library/string.h"
#include "kernel/tuple.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "storage/RelationBTree.h"
#include "library/MachineService.h"
#include "library/math.h"
#include "parser/ClauseBuilder.h"
#include "parser/TermBuilder.h"
#include "testing/fixtures.h"
#include "testing/testing.h"


void testCompilePermute1(void)
{
	// This rule compiles to a PERMUTE service with no constants
	// + z - x = y  <-  + x + y = z
	DictionaryEntry entry = DictionaryAddClauseFromCString("+ z - x = y | ! + x + y = z");
	Atom queryTerm = CStringToTerm("+ 7 - 4 = d");

	// This will yield a new service from the existing (+ + =) service
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// TODO: verify the compiled service atom types are correct

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 3);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom d = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "=", 1);
	ASSERT_UINT64_EQUAL(d._int, 3);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompilePermute2(void)
{
	// This rule compiles to a PERMUTE service with a constant 2.
	// The constant restricts an argument of the child service and cannot
	// introduce duplicate tuples, so no PROJECT service is needed.
	// number x addtwo y <- + x + 2 = y
	DictionaryEntry entry = DictionaryAddClauseFromCString("number x addtwo y | ! + x + 2 = y");
	Atom queryTerm = CStringToTerm("number 3 addtwo z");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom x = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "number", 1);
	ASSERT_UINT64_EQUAL(x._int, 3);

	Atom y = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "addtwo", 1);
	ASSERT_UINT64_EQUAL(y._int, 5);

	// Second call should fail (no more tuples)
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompileProject(void)
{
	// The variable p occurs in the clause but not in the query, so it obtains
	// an argument of its own, which is then dropped again by a PROJECT service:
	// the rule compiles to PROJECT(PERMUTE(...)).
	// set s element e <- list s position p element e
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"set s element e | ! list s position p element e");
	Atom queryTerm = CStringToTerm("set \"alibaba\" element e");

	// The element role is an untyped output, so the term matches every
	// (list position element) relation: one per element type. We therefore
	// get one compiled service per element type, and must enumerate them all.
	// Only the LETTER-element service yields tuples, as "alibaba" is a string;
	// the ID-element service is registered but matches nothing.
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 2)

	// The unique letters of "alibaba"
	char uniqueLetters[4] = "abil";
	index8 elementRoleIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(FormulaGetForm(queryTerm)),
		CreateNameFromCString("element")
	);
	int k = 0;
	for(index8 i = 0; i < nServices; i++) {
		ASSERT_FALSE(IsNullRelation(services[i].relation))
		ASSERT_NOT_NULL(services[i].op)

		Atom arguments[2];
		TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
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
		RemoveService(services[i].relation, services[i].op);
	}
	ReleaseFormula(queryTerm);
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
		"set s element e size n | ! list s position p element e");
	Atom queryTerm = CStringToTerm("set \"ab\" element e size z");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 0)

	for(index8 i = 0; i < nServices; i++) {
		RemoveService(services[i].relation, services[i].op);
	}
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompileJoin1(void)
{
	// This rule compiles to a JOIN service
	// first x second y third z  <-  + x + 1 = y & + y + 1 = z
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"first x second y third z | ! + x + 1 = y | ! + y + 1 = z");
	Atom queryTerm = CStringToTerm("first 3 second s third t");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 3);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom y = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "second", 1);
	ASSERT_UINT64_EQUAL(y._int, 4);

	Atom z = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "third", 1);
	ASSERT_UINT64_EQUAL(z._int, 5);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
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
		"first x third z | ! + x + 1 = y | ! + y + 1 = z");
	Atom queryTerm = CStringToTerm("first 3 third t");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom t = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "third", 1);
	ASSERT_UINT64_EQUAL(t._int, 5);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompileUnion(void)
{
	// Two rules resulting in a UNION service
	// number x neighbor y <- = y + x + 1     (y = x + 1)
	// number x neighbor y <- = x + y + 1     (x = y - 1 <-> y = x - 1)
	DictionaryEntry entry1 = DictionaryAddClauseFromCString(
		"number x neighbor y | ! = y + x + 1");
	DictionaryEntry entry2 = DictionaryAddClauseFromCString(
		"number x neighbor y | ! = x + y + 1");
	Atom queryTerm = CStringToTerm("number 5 neighbor y");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	// The atom types are encoded in the relation table associated with
	// the compiled service ...
	// PrintTuple(atomTypes?, arguments, 3);
	// PrintChar('\n');
	Atom y = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "neighbor", 1);
	ASSERT_TRUE(y._int == 4);

	ASSERT_TRUE(OperatorCall(context))
	// PrintTuple(arguments, 3);
	// PrintChar('\n');
	y = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "neighbor", 1);
	ASSERT_TRUE(y._int == 6);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry1);
	DictionaryRemoveClause(&entry2);
}


static RelationFixture edgeFixture;


/**
 * The variable x occurs twice in the term (edge e from x to x), which constrains
 * the two arguments providing it to be equal: the rule asks for the nodes of the
 * graph that have a self edge. The term compiles to a CONSTRAIN service, and e,
 * which does not occur in the query, is dropped again by a PROJECT service.
 */
void testCompileConstrain(void)
{
	SetupEdgeFixture(&edgeFixture);

	// self x <- edge e from x to x
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"self x | ! edge e from x to x");
	Atom queryTerm = CStringToTerm("self y");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
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
		foundA = foundA || SameAtoms(arguments[0], nodeA);
		foundB = foundB || SameAtoms(arguments[0], nodeB);
		nTuples++;
	}
	OperatorFreeContext(context);

	ASSERT_UINT32_EQUAL(nTuples, 2)
	ASSERT_TRUE(foundA)
	ASSERT_TRUE(foundB)

	IFactRelease(nodeA);
	IFactRelease(nodeB);
	RemoveService(services[0].relation, services[0].op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry);
	TeardownRelationFixture(&edgeFixture);
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
		"number n faculty f | ! + m + 1 = n | ! number m faculty e | ! * e * n = f");
	// Create terminating fact, provide by a B-tree service
	Atom terminatingFact = CStringToTerm("number 0 faculty 1");	
	Relation relation = CreateRelation(
		FormulaGetForm(terminatingFact),
		CreateTypeSignature(
			TypedTuplePeekAtomTypes(FormulaGetActors(terminatingFact)), 2)
	);
	RelationTable * table = CreateRelationTable(
		relation, &btreeStorageProvider, (index8[]) {0, 1}, 2);
	ReleaseRelation(relation);
	RelationTableAddTuple(table, TypedTuplePeekAtoms(FormulaGetActors(terminatingFact)), 0);
	// Compile the query
	Atom queryTerm = CStringToTerm("number 4 faculty f");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 3);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom f = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "faculty", 1);
	ASSERT_UINT64_EQUAL(f._int, 24);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
	RelationTableRemoveTuple(table, TypedTuplePeekAtoms(FormulaGetActors(terminatingFact)), 0);
	ReleaseRelationTable(table);
	ReleaseFormula(terminatingFact);
	DictionaryRemoveClause(&entry);
}


static RelationFixture precSuccFixture;


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
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	AddTransitiveClosureRules(&entry1, &entry2);

	Atom queryTerm = CStringToTerm("before \"a\" after \"d\"");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the service. Both arguments are bound, so it yields the query tuple itself
	// if the relation holds it, and nothing otherwise.
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom before = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "before", 1);
	Atom after = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "after", 1);
	Atom nodeA = CreateStringFromCString("a");
	Atom nodeD = CreateStringFromCString("d");
	ASSERT_UINT64_EQUAL(before.hash, nodeA.hash)
	ASSERT_UINT64_EQUAL(after.hash, nodeD.hash)
	IFactRelease(nodeA);
	IFactRelease(nodeD);

	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	TeardownRelationFixture(&precSuccFixture);
}


/**
 * The same rules with the after role left free, asking for every node reaching d from a.
 * This is the query the derivation is driven by its call bindings for: the nodes after a
 * are b, c and d, and the component e -> f is never derived, as nothing calls for it.
 */
void testCompileRecursiveReachable(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	AddTransitiveClosureRules(&entry1, &entry2);

	Atom queryTerm = CStringToTerm("before \"a\" after y");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// The nodes after a, which the fixpoint yields in its own order
	char const * expectedNodes[3] = {"b", "c", "d"};
	bool found[3] = {false, false, false};

	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	size32 nTuples = 0;
	while(OperatorCall(context)) {
		Atom after = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "after", 1);
		for(index8 i = 0; i < 3; i++) {
			Atom node = CreateStringFromCString(expectedNodes[i]);
			found[i] = found[i] || SameAtoms(after, node);
			IFactRelease(node);
		}
		nTuples++;
	}
	OperatorFreeContext(context);

	ASSERT_UINT32_EQUAL(nTuples, 3)
	for(index8 i = 0; i < 3; i++)
		ASSERT_TRUE(found[i])

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	TeardownRelationFixture(&precSuccFixture);
}


/**
 * This test attemps to compile the term (reach "a" hop b) given the recursive rule
 * 
 *   reach a hop b <- reach c hop b & prec c succ a
 * 
 * This leads the compiler to the conjunction (reach c hop b & prec c succ <ID)
 * 
 * 
 * The derivation of a recursive relation is keyed on the arguments the query binds, so a
 * recursive term that leaves one of them free has no call binding to name it and the clause
 * is refused; see compileRecursiveTerm(). Here the recursive term of
 *
 *   reach a hop b <- reach c hop b & prec c succ a
 *
 * walks the graph backwards, so it leaves the argument the query binds free. The clause
 * does not compile, and the query is answered by the base clause alone.
 */
void testCompileRecursiveTermUnboundInput(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry baseEntry = DictionaryAddClauseFromCString(
		"reach a hop b | ! prec a succ b");
	DictionaryEntry recursiveEntry = DictionaryAddClauseFromCString(
		"reach a hop b | ! reach c hop b | ! prec c succ a");

	Atom queryTerm = CStringToTerm("reach \"a\" hop y");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)

	// Only the successors of a, which is b alone, and not the closure b, c, d
	Atom nodeB = CreateStringFromCString("b");
	size32 nTuples = 0;
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
	void * context = OperatorCreateContext(services[0].op, arguments);
	while(OperatorCall(context)) {
		Atom hop = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "hop", 1);
		ASSERT_TRUE(SameAtoms(hop, nodeB))
		nTuples++;
	}
	OperatorFreeContext(context);
	ASSERT_UINT32_EQUAL(nTuples, 1)

	IFactRelease(nodeB);
	RemoveService(services[0].relation, services[0].op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&recursiveEntry);
	DictionaryRemoveClause(&baseEntry);
	TeardownRelationFixture(&precSuccFixture);
	// The above leaves behind a (succ <ID prec >ID) compiled service generated as part
	// of the process of compiling (reach "a" hop y) query, which is not invalidated
	// by removing the rules. Clean this out.
	RemoveAllCompiledServices();
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
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	AddTransitiveClosureRules(&entry1, &entry2);

	Atom queryTerm = CStringToTerm("before x after y");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	char const * expectedBefore[PREC_SUCC_N_CLOSURE_TUPLES] = {
		"a", "a", "a", "b", "b", "b", "c", "c", "c", "e"};
	char const * expectedAfter[PREC_SUCC_N_CLOSURE_TUPLES] = {
		"b", "c", "d", "b", "c", "d", "b", "c", "d", "f"};
	bool found[PREC_SUCC_N_CLOSURE_TUPLES] = {false};

	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);
	size32 nTuples = 0;
	while(OperatorCall(context)) {
		Atom before = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "before", 1);
		Atom after = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "after", 1);
		for(index8 i = 0; i < PREC_SUCC_N_CLOSURE_TUPLES; i++) {
			Atom expectedBeforeNode = CreateStringFromCString(expectedBefore[i]);
			Atom expectedAfterNode = CreateStringFromCString(expectedAfter[i]);
			if(SameAtoms(before, expectedBeforeNode)
				&& SameAtoms(after, expectedAfterNode))
				found[i] = true;
			IFactRelease(expectedBeforeNode);
			IFactRelease(expectedAfterNode);
		}
		nTuples++;
	}
	OperatorFreeContext(context);

	ASSERT_UINT32_EQUAL(nTuples, PREC_SUCC_N_CLOSURE_TUPLES)
	for(index8 i = 0; i < PREC_SUCC_N_CLOSURE_TUPLES; i++)
		ASSERT_TRUE(found[i])

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	TeardownRelationFixture(&precSuccFixture);
}


/**
 * CLAUDE: The same graph together with a second (prec succ) relation over integers, which
 * the same rules give a closure of its own. The query names no types, so it compiles one
 * variant per relation, and the recursive clause has to compile into both of them. A
 * recursive clause compiled against one variant alone leaves the other its edges only;
 * see compileQueryVariants().
 */
void testCompileRecursiveVariants(void)
{
	// The (prec succ) fixture with types {AT_ID, AT_ID}
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry baseRule;
	DictionaryEntry recursiveRule;
	AddTransitiveClosureRules(&baseRule, &recursiveRule);

	// Add a second (prec succ) relation with types {AT_INT, AT_INT},
	// defining a separate graph.
	Relation precSuccIntRelation = CreateRelation(
		precSuccFixture.termForm, CreateTypeSignature((byte[]) {AT_INT, AT_INT}, 2));
	RelationTable * precSuccIntTable = CreateRelationTable(
		precSuccIntRelation, &btreeStorageProvider, (index8[]) {0, 1}, 2);
	ReleaseRelation(precSuccIntRelation);
	// Add the facts (prec 1 succ 2), (prec 2 succ 3)
	index8 precRoleIndex = RelationFixtureRoleIndex(&precSuccFixture, "prec");
	index8 succRoleIndex = RelationFixtureRoleIndex(&precSuccFixture, "succ");
	Atom precSuccIntEdges[2][2];
	for(index8 i = 0; i < 2; i++) {
		precSuccIntEdges[i][precRoleIndex] = (Atom) {._int = 1 + i};
		precSuccIntEdges[i][succRoleIndex] = (Atom) {._int = 2 + i};
		RelationTableAddTuple(precSuccIntTable, precSuccIntEdges[i], 0);
	}
	// The query (before x after y) should now generate a (before after) service
	// for both the AT_ID and AT_INT versions, seeded by the non-recursive rule
	// (before x after y <- prec x succ y)
	Atom queryTerm = CStringToTerm("before x after y");
	Service compiledServices[MAX_COMPILED_SERVICES];
	size8 nCompiledServices = CompileQuery(queryTerm, compiledServices);
	ASSERT_UINT32_EQUAL(nCompiledServices, 2)

	// Expected values for the transitive closure of the AT_INT relation
	int64 expectedBefore[3] = {1, 2, 1};
	int64 expectedAfter[3] = {2, 3, 3};
	bool foundIntTuple[3] = {false, false, false};

	size32 nIntTuples = 0;
	size32 nIdTuples = 0;
	for(index8 i = 0; i < nCompiledServices; i++) {
		bool intService = (compiledServices[i].relation.typeSignature.atomTypes[0] == AT_INT);
		Atom arguments[2];
		TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
		void * context = OperatorCreateContext(compiledServices[i].op, arguments);
		while(OperatorCall(context)) {
			if(!intService) {
				// we have the AT_ID relation
				nIdTuples++;
				continue;
			}
			// else we have AT_INT relation, check tuple
			Atom before = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "before", 1);
			Atom after = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "after", 1);
			for(index8 j = 0; j < 3; j++)
				foundIntTuple[j] = foundIntTuple[j] || (
					(before._int == expectedBefore[j]) && (after._int == expectedAfter[j])
				);
			nIntTuples++;
		}
		OperatorFreeContext(context);
	}

	// Check that all tuples in each graph's transitive closure are found
	ASSERT_UINT32_EQUAL(nIdTuples, PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(nIntTuples, 3)
	for(index8 i = 0; i < 3; i++)
		ASSERT_TRUE(foundIntTuple[i])

	// Cleanup
	for(index8 i = 0; i < nCompiledServices; i++)
		RemoveService(compiledServices[i].relation, compiledServices[i].op);
	ReleaseFormula(queryTerm);
	for(index8 i = 0; i < 2; i++)
		RelationTableRemoveTuple(precSuccIntTable, precSuccIntEdges[i], 0);
	ReleaseRelationTable(precSuccIntTable);
	DictionaryRemoveClause(&recursiveRule);
	DictionaryRemoveClause(&baseRule);
	TeardownRelationFixture(&precSuccFixture);
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
	Atom odd3term = CStringToTerm("odd 3");
	Relation evenRelation = CreateRelation(
		FormulaGetForm(odd3term), CreateTypeSignature((byte[]) {AT_INT}, 1));
	RelationTable * evenTable = CreateRelationTable(
		evenRelation, &btreeStorageProvider, (index8[]) {0}, 1);
	ReleaseRelation(evenRelation);
	RelationTableAddTuple(evenTable, TypedTuplePeekAtoms(FormulaGetActors(odd3term)), 0);
	// setup the rule
	DictionaryEntry entry = DictionaryAddClauseFromCString("! even x | ! odd x");
	Atom queryTerm = CStringToTerm("! even 3");

	// compile the query
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// Call the resulting service
	Atom arguments[1];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 1);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))

	Atom x = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "even", 1);
	ASSERT_UINT64_EQUAL(x._int, 3);

	// Second call should fail (no more tuples)
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	// teardown
	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
	
	DictionaryRemoveClause(&entry);

	RelationTableRemoveTuple(evenTable, TypedTuplePeekAtoms(FormulaGetActors(odd3term)), 0);
	ReleaseRelationTable(evenTable);
	ReleaseFormula(odd3term);
}


/**
 * A compiled service reads its stored relations live through their MACHINE operators, so
 * asserting or retracting a fact of a relation that already exists needs no invalidation:
 * the service compiled before the change answers correctly after it. Only structural
 * change is invalidated; see the notes on invalidation in compiler.md.
 */
void testCompiledServiceReadsFactsLive(void)
{
	// (odd 3), and the rule making (! even x) follow from (odd x)
	Atom odd3term = CStringToTerm("odd 3");
	Relation oddRelation = CreateRelation(
		FormulaGetForm(odd3term), CreateTypeSignature((byte[]) {AT_INT}, 1));
	RelationTable * oddTable = CreateRelationTable(
		oddRelation, &btreeStorageProvider, (index8[]) {0}, 1);
	ReleaseRelation(oddRelation);
	RelationTableAddTuple(oddTable, TypedTuplePeekAtoms(FormulaGetActors(odd3term)), 0);
	DictionaryEntry entry = DictionaryAddClauseFromCString("! even x | ! odd x");

	Atom queryTerm = CStringToTerm("! even 3");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];
	size32 nCompiled = NumberOfCompiledServices();

	Atom arguments[1];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 1);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))
	OperatorFreeContext(context);

	// Retracting the fact leaves the service registered, as the relation still exists
	RelationTableRemoveTuple(oddTable, TypedTuplePeekAtoms(FormulaGetActors(odd3term)), 0);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), nCompiled)

	// and that same service now yields nothing, having read the change
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 1);
	context = OperatorCreateContext(service.op, arguments);
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	// asserting it again brings the answer back, still without recompiling
	RelationTableAddTuple(oddTable, TypedTuplePeekAtoms(FormulaGetActors(odd3term)), 0);
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 1);
	context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))
	OperatorFreeContext(context);

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry);
	RelationTableRemoveTuple(oddTable, TypedTuplePeekAtoms(FormulaGetActors(odd3term)), 0);
	ReleaseRelationTable(oddTable);
	ReleaseFormula(odd3term);
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
		"number n square s | ! lower 1 number n upper 4 | ! * n * n = s");
	Atom queryTerm = CStringToTerm("number n square s");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
	void * context = OperatorCreateContext(service.op, arguments);

	for(int64 expected = 1; expected <= 4; expected++) {
		ASSERT_TRUE(OperatorCall(context))
		Atom n = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "number", 1);
		ASSERT_INT64_EQUAL(n._int, expected)
		Atom s = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "square", 1);
		ASSERT_INT64_EQUAL(s._int, expected * expected)
	}
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


/**
 * A rule body term with no service of its own is compiled from the rules answering it, so
 * one rule can be built on another. Here (grandparent grandchild) is defined over
 * (parent offspring), which is itself a rule over the stored (father child) relation, and
 * neither of the two (parent offspring) services exists until this query compiles them.
 *
 * The two terms of the grandparent rule ask for different IO patterns: the first leaves
 * both arguments free, and the second takes as an input the argument the first produced.
 * So the rule compiles twice, once per pattern.
 */
static RelationFixture fatherFixture;

void testCompileChainedRules(void)
{
	SetupRelationFixture(&fatherFixture, (char const * []) {"father", "child"}, 2);
	RelationFixtureAddTuple(&fatherFixture, (char const * []) {"a", "b"});
	RelationFixtureAddTuple(&fatherFixture, (char const * []) {"b", "c"});

	// parent p offspring c <- father p child c
	DictionaryEntry parentEntry = DictionaryAddClauseFromCString(
		"parent p offspring c | ! father p child c");
	// grandparent x grandchild z <- parent x offspring y & parent y offspring z
	DictionaryEntry grandparentEntry = DictionaryAddClauseFromCString(
		"grandparent x grandchild z | ! parent x offspring y | ! parent y offspring z");

	size32 nCompiledBefore = NumberOfCompiledServices();
	Atom queryTerm = CStringToTerm("grandparent x grandchild z");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)

	// The query service, and one (parent offspring) service per IO pattern its two terms
	// asked for
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices() - nCompiledBefore, 3)

	// Only a has a grandchild, which is c
	Atom nodeA = CreateStringFromCString("a");
	Atom nodeC = CreateStringFromCString("c");
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
	void * context = OperatorCreateContext(services[0].op, arguments);
	ASSERT_TRUE(OperatorCall(context))
	Atom x = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "grandparent", 1);
	Atom z = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "grandchild", 1);
	ASSERT_TRUE(SameAtoms(x, nodeA))
	ASSERT_TRUE(SameAtoms(z, nodeC))
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	IFactRelease(nodeA);
	IFactRelease(nodeC);
	RemoveService(services[0].relation, services[0].op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&grandparentEntry);
	DictionaryRemoveClause(&parentEntry);
	// Dropping the stored relation invalidates the compiled (parent offspring) services
	TeardownRelationFixture(&fatherFixture);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), nCompiledBefore)
}


/**
 * A term is only offered to the rules once no term of the clause dispatches to a service
 * that exists, so a term the rules answer never compiles ahead of a term that would bind
 * its arguments. Here (start point) binds the argument (alias as) takes as an input, and
 * the two terms have different forms, so which of them the clause iterates first is not
 * something the test can arrange.
 *
 * The service compiled for the alias term is what shows the order taken: with the term
 * bound there is a service taking the alias argument as an input, and none reading the
 * relation unbound.
 */
static RelationFixture nodeFixture;
static RelationFixture startFixture;

void testCompileChainedRuleOrder(void)
{
	SetupRelationFixture(&nodeFixture, (char const * []) {"node", "label"}, 2);
	RelationFixtureAddTuple(&nodeFixture, (char const * []) {"na", "la"});
	RelationFixtureAddTuple(&nodeFixture, (char const * []) {"nb", "lb"});
	SetupRelationFixture(&startFixture, (char const * []) {"start", "point"}, 2);
	RelationFixtureAddTuple(&startFixture, (char const * []) {"sa", "na"});

	// alias k as l <- node k label l
	DictionaryEntry aliasEntry = DictionaryAddClauseFromCString(
		"alias k as l | ! node k label l");
	// pick p give g <- start p point k & alias k as g
	DictionaryEntry pickEntry = DictionaryAddClauseFromCString(
		"pick p give g | ! start p point k | ! alias k as g");

	Atom queryTerm = CStringToTerm("pick \"sa\" give g");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)

	Atom labelA = CreateStringFromCString("la");
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 2);
	void * context = OperatorCreateContext(services[0].op, arguments);
	ASSERT_TRUE(OperatorCall(context))
	Atom g = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "give", 1);
	ASSERT_TRUE(SameAtoms(g, labelA))
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);
	IFactRelease(labelA);

	// The alias term compiled with its argument bound, and never unbound
	Service aliasService;
	index8 aliasPermutation[2];
	Atom boundAlias = CStringToTerm("alias \"na\" as l");
	ASSERT_TRUE(DispatchQueryFormula(boundAlias, &aliasService, aliasPermutation))
	ReleaseFormula(boundAlias);
	Atom unboundAlias = CStringToTerm("alias k as l");
	ASSERT_FALSE(DispatchQueryFormula(unboundAlias, &aliasService, aliasPermutation))
	ReleaseFormula(unboundAlias);

	RemoveService(services[0].relation, services[0].op);
	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&pickEntry);
	DictionaryRemoveClause(&aliasEntry);
	TeardownRelationFixture(&startFixture);
	TeardownRelationFixture(&nodeFixture);
}


/**
 * Two rules recursive through one another have no base case: compiling (p) reaches (q),
 * which reaches (p) again. A parameterized query already being compiled yields no service,
 * so the clause fails to compile and the compilation terminates, which is what this test
 * is here to show. Mutual recursion is a gap; see compiler.md.
 */
void testCompileMutualRecursion(void)
{
	DictionaryEntry pEntry = DictionaryAddClauseFromCString("p x | ! q x");
	DictionaryEntry qEntry = DictionaryAddClauseFromCString("q x | ! p x");

	Atom queryTerm = CStringToTerm("p n");
	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 0)

	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&qEntry);
	DictionaryRemoveClause(&pEntry);
}


/**
 * Compile a service for an IO pattern no service provides. The B-tree registers a service
 * per prefix of its index column order, so binding the element without binding the position
 * has none, and the query compiles to a FILTER operator over the service that produces the
 * element; see compileFilterVariants().
 */
void testCompileNewIOPattern(void)
{
	Atom queryTerm = CStringToTerm("list \"AB\" position _ element 'A");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// 'A is the first letter of "AB"
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 3);
	void * context = OperatorCreateContext(service.op, arguments);
	ASSERT_TRUE(OperatorCall(context))
	Atom position = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "position", 1);
	ASSERT_INT64_EQUAL(position._int, 1)
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
}


/**
 * A filtered service yields every tuple that matches, in the order its child yields them.
 * The letter 'a of "alibaba" occurs at positions 1, 5 and 7.
 */
void testCompileNewIOPatternRepeated(void)
{
	Atom queryTerm = CStringToTerm("list \"alibaba\" position _ element 'a");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	int64 const expectedPositions[] = {1, 5, 7};
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 3);
	void * context = OperatorCreateContext(service.op, arguments);
	for(index8 i = 0; i < 3; i++) {
		ASSERT_TRUE(OperatorCall(context))
		Atom position = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "position", 1);
		ASSERT_INT64_EQUAL(position._int, expectedPositions[i])
	}
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	RemoveService(service.relation, service.op);
	ReleaseFormula(queryTerm);
}


/**
 * A term of a rule body can need a filter just as a query can. Here the body term
 * (list s position p element e) is asked with the element bound and the position free,
 * which no service provides, so compiling the term reaches compileFilterVariants() through
 * the rules fallback of dispatchOrCompileTerm(). The filter sits under the PERMUTE operator
 * that binds the term constants.
 */
void testCompileFilterInRuleBody(void)
{
	size32 nCompiledBefore = NumberOfCompiledServices();
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"at s position p letter e | ! list s position p element e");
	Atom queryTerm = CStringToTerm("at \"abracadabra\" position q letter 'a");

	Service services[MAX_COMPILED_SERVICES];
	size8 nServices = CompileQuery(queryTerm, services);
	ASSERT_UINT32_EQUAL(nServices, 1)
	Service service = services[0];

	// 'a occurs at positions 1, 4, 6, 8, 11 of "abracadabra"
	int64 const expectedPositions[] = {1, 4, 6, 8, 11};
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(FormulaGetActors(queryTerm)), arguments, 3);
	void * context = OperatorCreateContext(service.op, arguments);
	for(index8 i = 0; i < 5; i++) {
		ASSERT_TRUE(OperatorCall(context))
		Atom position = TermGetRoleActor(FormulaGetForm(queryTerm), arguments, "position", 1);
		ASSERT_INT64_EQUAL(position._int, expectedPositions[i])
	}
	ASSERT_FALSE(OperatorCall(context))
	OperatorFreeContext(context);

	// Compiling the query also compiled a service for its body term, which is a cache
	// over the list relation and outlives the rule; remove both
	RemoveService(service.relation, service.op);
	RemoveAllCompiledServices();
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), nCompiledBefore)

	ReleaseFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


/**
 * A filtered service is compiled because no rule answered the query, so adding a rule for
 * the query term form must remove it again; see ServiceRegistryInvalidateByTermForm().
 */
void testFilterServiceInvalidatedByRule(void)
{
	size32 nCompiledBefore = NumberOfCompiledServices();
	Atom queryTerm = CStringToTerm("list \"AB\" position _ element 'A");
	Service services[MAX_COMPILED_SERVICES];
	ASSERT_UINT32_EQUAL(CompileQuery(queryTerm, services), 1)
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), nCompiledBefore + 1)

	// A rule of the query's term form invalidates what was compiled for that form
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"list s position p element e | ! at s index p letter e");
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), nCompiledBefore)

	DictionaryRemoveClause(&entry);
	ReleaseFormula(queryTerm);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	ListSetup();
	StringSetup();
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
	ExecuteTest(testCompileRecursiveVariants);
	ExecuteTest(testCompileRecursiveTermUnboundInput);

	ExecuteTest(testCompileNegatedTerm);
	ExecuteTest(testCompiledServiceReadsFactsLive);
	ExecuteTest(testCompileSquares);

	ExecuteTest(testCompileChainedRules);
	ExecuteTest(testCompileChainedRuleOrder);
	ExecuteTest(testCompileMutualRecursion);

	ExecuteTest(testCompileNewIOPattern);
	ExecuteTest(testCompileNewIOPatternRepeated);
	ExecuteTest(testCompileFilterInRuleBody);
	ExecuteTest(testFilterServiceInvalidatedByRule);

	// TODO: compiling a recursive rule over an infinite domain. The relation has no
	// finite fixpoint and the call bindings n = 4, 3, 2, 1, 0, -1, -2, ... do not
	// terminate either, so this needs the precondition ? < n > 0: to guard the
	// recursive clause; see the notes on termination in compiler.md.
	// ExecuteTest(testCompileRecursiveJoin1);

	FreeMachineServices();
	StringShutdown();
	ListShutdown();
	TestSummary();
}
