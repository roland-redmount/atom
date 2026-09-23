
#include "compiler/compiler.h"
#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/library.h"
#include "library/MachineService.h"
#include "library/string.h"
#include "storage/RelationBTree.h"
#include "parser/FormulaBuilder.h"
#include "parser/TermBuilder.h"
#include "testing/fixtures.h"
#include "testing/testing.h"
#include "ui/query.h"


static RelationFixture precSuccFixture;


/**
 * Run a UserQuery (possibly compiling the query) and count the number of tuples
 * in the resulting relations
 */
static size32 runUserQueryAndCountTuples(char const * queryString)
{
	// CLAUDE: The query may be a term or a conjunction
	Atom query = CStringToFormula(queryString);
	FormulaView queryView = FormulaGetView(query);
	MixedTypeRelation * relation = UserQuery(queryView);
	size32 nTuples = 0;
	while(MixedTypeRelationNext(relation)) {
		// TypedTuple const * tuple = MixedTypeRelationPeekTuple(relation);
		// PrintFormActorsAsFormula(queryView.form, tuple);
		// PrintChar('\n');
		nTuples++;
	}
	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);
	return nTuples;
}


/**
 * A query answered by the facts of a stored relation needs no rule and no compilation:
 * it is answered by the services the relation was registered with.
 */
void testQueryStoredFacts(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	size32 nServices = NumberOfServices();

	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("prec x succ y"), PREC_SUCC_N_EDGES)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices)

	TeardownRelationFixture(&precSuccFixture);
}


/**
 * An integer literal in a query dispatches to a kernel service taking an INT column.
 * The tokenizer gives every integer literal the type AT_INT, and dispatch matches a
 * query parameter type against a service column type by equality, so naming a list
 * position by literal only finds a service while (list position element) keeps its
 * position column INT; see signatureQueryTupleMatch() in dispatch.c.
 */
void testQueryIntegerLiteral(void)
{
	// "ab" is a list of letters, so only the LETTER service answers; the ID service
	// of the same form contributes nothing
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("list \"ab\" position 1 element e"), 1)
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("list \"ab\" position 2 element e"), 1)
	// a position no element of the list has
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("list \"ab\" position 3 element e"), 0)
	// the length of the list, whose service takes an INT column too
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("list \"ab\" length 2"), 1)
}


/**
 * A query no service answers is compiled when it is first asked, and answered by the
 * compiled services from then on. The rules here derive the transitive closure of the
 * graph, so the answer holds tuples that are not facts of any stored relation.
 */
void testQueryCompilesOnce(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	AddTransitiveClosureRules(&entry1, &entry2);
	size32 nServices = NumberOfServices();

	// The first query compiles the service deriving the closure
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 1)

	// The same query is answered by that service, and compiles nothing further
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 1)

	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	TeardownRelationFixture(&precSuccFixture);
}


/**
 * Two queries of the same term form compile to distinct services if their parameter IO
 * direction (position of variables) differ. Here, the query (before x after y) is distinct
 * from (before "a" after y) since the parameter IO direction differs, so they compile
 * independently to two distinct services.
 */
void testQueryParameterIO(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	AddTransitiveClosureRules(&entry1, &entry2);
	size32 nServices = NumberOfServices();

	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 1)

	// b, c and d come after a, and this query compiles a service of its own
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before \"a\" after y"), 3)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 2)
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before \"a\" after y"), 3)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 2)

	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	TeardownRelationFixture(&precSuccFixture);
}


/**
 * Test that UserQuery() filters correctly on repeated variables.
 */
void testQueryRepeatedVariable(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	AddTransitiveClosureRules(&entry1, &entry2);
	size32 nServices = NumberOfServices();

	// b and c lie on a cycle, and so come after themselves
	// CLAUDE: This compiles the service (before @1 after @1) repeating a parameter. Its
	// recursive clause reads the distinct service (before >ID after <ID), which is
	// compiled as well; see termRepeatsQueryParameters() in compiler.c.
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before x after x"), 2)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 2)

	// This query yields no tuples, since the (list position element) service
	// has distinct parameter types for the position and element roles, and
	// therefore all tuples from the servuce will fail the equality constraint.
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("list \"ab\" position x element x"), 0)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 2)

	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	TeardownRelationFixture(&precSuccFixture);
}


/**
 * Since a repeated variable is lost when generalizing a query to parameters, the
 * queries (item e index p) and (item z index z) below should compile to the same
 * service, and therefore only the first query leads to compilation, while the
 * second query re-uses the existing compiled service.
 */
void testQueryCompileIgnoresRepeatedVariable(void)
{
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"item e index p | ! list \"ab\" position p element e");
	size32 nServices = NumberOfServices();

	// This query compiles two services since we have two (list position element)
	// services with element types AT_ID and AT_NAME; it yields the letters of "ab"
	// and their positions.
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("item e index p"), 2)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 2)

	// This query re-uses the above compiled services, but yields no tuples
	// since the element type is never an INT.
	// CLAUDE: The query repeats a parameter, so it does not re-use the above services.
	// It compiles no service, since no (list position element) service can repeat a
	// parameter at the INT position and the element.
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("item z index z"), 0)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 2)

	DictionaryRemoveClause(&entry);
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices)
}


/**
 * A query that no fact and no rule answers has no tuples, and compiles nothing. It is
 * compiled again every time it is asked, which is what lets it start working once a rule
 * answering it is asserted.
 */
void testQueryWithoutAnswer(void)
{
	size32 nServices = NumberOfServices();

	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("nowhere x nothing y"), 0)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices)
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("nowhere x nothing y"), 0)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices)
}


/**
 * Test that a compiled service is invalidated when adding a new relation for
 * a term the compilation depends on.
 */
void testInvalidateServiceByNewRelation(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	AddTransitiveClosureRules(&entry1, &entry2);

	// compile and run (before x after y)
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 1)

	// Add a second Relation of the (prec succ) form, but with different type signature,
	// creating new primitive services not present during the compilation above.
	// This should invalidate the compiled service, since it now depends on the new relation.
	Relation intRelation = {
		.form = precSuccFixture.termForm,
		.typeSignature = CreateTypeSignature((byte[]) {AT_ID, AT_INT}, 2)
	};
	CreateTupleStore(intRelation, &btreeStorageProvider, 2, 0);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)

	// Querying again compiles an additional service for the (before:ID after:INT) relation.
	// Since the (prec:ID succ:INT) relation was empty, there are no additional tuples.
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 2)
	
	// Cleanup
	RemoveAllCompiledServices();
	DropRelation(intRelation);
	TeardownRelationFixture(&precSuccFixture);
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
}


/**
 * Test that adding or removing a rule (clause) invalidates services whose term form
 * occurs in the new rule.
 */
void testInvalidateServiceByRule(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry rule1 = DictionaryAddClauseFromCString(
		"before x after y | ! prec x succ y");
	size32 nServicesInitial = NumberOfServices();

	// Given rule1, the (before x after y) query returns edge relation itself
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before x after y"), PREC_SUCC_N_EDGES)
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 1)

	// Add a transitive rule
	DictionaryEntry rule2 = DictionaryAddClauseFromCString(
		"before x after y | ! prec x succ z | ! before z after y");
	// The previously compiled service is now invalidated
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	// Compile and run the (before x after y) query again,
	// now returns the graph closure
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 1)

	// Removing rule 1 returns us to the previous state
	DictionaryRemoveClause(&rule2);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before x after y"), PREC_SUCC_N_EDGES)

	DictionaryRemoveClause(&rule1);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServicesInitial)
	TeardownRelationFixture(&precSuccFixture);
}

size32 numberOfStaleServices(Relation relation)
{
	ServiceIterator iterator;
	ServiceRegistryIterate(relation, &iterator);
	size32 nStale = 0;
	while(ServiceIteratorNext(&iterator)) {
		if(ServiceIteratorPeekRecord(&iterator)->isStale)
			nStale++;
	}
	ServiceIteratorEnd(&iterator);
	return nStale;
}


/**
 * Same example as testCompileRecursiveJoin2() in test_compiler.c, but here we create the rule
 * _after_ creating the stored tuple. This requires invalidating the relation so that
 * compilation will be triggered.
 */
void testInvalidateRelationByRule(void)
{
	// Create terminating fact
	Atom terminatingFact = CStringToTerm("number 0 faculty 1");	
	Relation relation = RelationFromFact(FormulaGetView(terminatingFact));
	TupleStore * store = CreateTupleStore(relation, &btreeStorageProvider, 2, 0);
	TupleStoreAddTuple(store, TypedTuplePeekAtoms(FormulaGetActors(terminatingFact)), 0);
	Atom faculty = CreateNameFromCString("faculty");
	index8 facultyColumn = PredicateRoleIndex(TermFormGetPredicateForm(relation.form), faculty);
	NameRelease(faculty);

	// Adding the recursive rule should mark the service as stale
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"number n faculty f | ! < n > 0 | ! + m + 1 = n | ! number m faculty e | ! * e * n = f");
	ASSERT_INT32_EQUAL(numberOfStaleServices(relation), 3)

	// Run a query to trigger re-compilation
	Atom queryTerm = CStringToTerm("number 4 faculty f");
	MixedTypeRelation * mixedTypeRelation = UserQuery(FormulaGetView(queryTerm));
	ASSERT_TRUE(MixedTypeRelationNext(mixedTypeRelation))
	TypedTuple const * resultTuple = MixedTypeRelationPeekTuple(mixedTypeRelation);
	ASSERT_INT64_EQUAL(TypedTupleGetElement(resultTuple, facultyColumn).atom._int, 24);
	ASSERT_FALSE(MixedTypeRelationNext(mixedTypeRelation))
	FreeMixedTypeRelation(mixedTypeRelation);

	ReleaseFormula(queryTerm);
	RelationRemoveTuple(relation, TypedTuplePeekAtoms(FormulaGetActors(terminatingFact)), 0);
	DictionaryRemoveClause(&entry);
	DropRelation(relation);
	ReleaseFormula(terminatingFact);
}


/**
 * Test that compiling a query for a stale relation clears its stale flag, even when the query
 * matches an existing primitive service so that no new service is compiled.
 */
void testCompileClearsStaleRelation(void)
{
	SetupPrecSuccFixture(&precSuccFixture);

	// Mark the (prec >ID succ >ID) service stale
	Atom query = CStringToTerm("prec x succ y");
	Service service;
	index8 permutation[2];
	ASSERT_INT32_EQUAL(
		DispatchQuery(FormulaGetView(query), &service, permutation),
		DISPATCH_FOUND
	)
	ServiceMarkStale(service);
	ASSERT_TRUE(ServiceIsStale(service))

	// Compiling the query considers the relation and clears the flag, although the
	// query matches the existing primitive service and registers no new service
	ASSERT_INT32_EQUAL(CompileQuery(FormulaGetView(query), 0), 0);
	ASSERT_FALSE(ServiceIsStale(service))
	ReleaseFormula(query);

	TeardownRelationFixture(&precSuccFixture);
}


/**
 * Test that adding or removing a rule marks primitive services as stale.
 */
void testStaleClearedAfterRuleRemoved(void)
{
	SetupPrecSuccFixture(&precSuccFixture);

	// Add a rule matching the stored (prec succ) relation
	DictionaryEntry rule = DictionaryAddClauseFromCString("prec x succ y | ! before x after y");
	ASSERT_UINT32_EQUAL(numberOfStaleServices(precSuccFixture.relation), 3)
	// Running the query triggers compilation of the (prec >ID succ >ID) service
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("prec x succ y"), PREC_SUCC_N_EDGES)
	ASSERT_UINT32_EQUAL(numberOfStaleServices(precSuccFixture.relation), 2)
	
	// Same, when removing the rule
	DictionaryRemoveClause(&rule);
	ASSERT_UINT32_EQUAL(numberOfStaleServices(precSuccFixture.relation), 3)
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("prec x succ y"), PREC_SUCC_N_EDGES)
	ASSERT_UINT32_EQUAL(numberOfStaleServices(precSuccFixture.relation), 2)

	TeardownRelationFixture(&precSuccFixture);
}


/**
 * A rule body term whose relation is stale (its own rule not yet compiled) must
 * trigger recompilation, so that the body sees the rule-derived tuples and not just the
 * primitive service.
 */
void testStaleBodyTermUsesPrimitiveOnly(void)
{
	// Relation (alpha beta) storing one fact ("a" "b")
	Atom alphaBetaFact = CStringToTerm("alpha \"a\" beta \"b\"");
	Relation alphaBetaRelation = RelationFromFact(FormulaGetView(alphaBetaFact));
	TupleStore * alphaBetaStore = CreateTupleStore(alphaBetaRelation, &btreeStorageProvider, 2, 0);
	TupleStoreAddTuple(alphaBetaStore, TypedTuplePeekAtoms(FormulaGetActors(alphaBetaFact)), 0);

	// Relation (gamma delta) storing one fact ("c" "d")
	Atom gammaDeltaFact = CStringToTerm("gamma \"c\" delta \"d\"");
	Relation gammaDeltaRelation = RelationFromFact(FormulaGetView(gammaDeltaFact));
	TupleStore * gammaDeltaStore = CreateTupleStore(gammaDeltaRelation, &btreeStorageProvider, 2, 0);
	TupleStoreAddTuple(gammaDeltaStore, TypedTuplePeekAtoms(FormulaGetActors(gammaDeltaFact)), 0);

	// Add rule giving (alpha beta) the tuples of (gamma delta), rendering all
	// primitive services of the (alpha beta) relation stale.
	DictionaryEntry aRule = DictionaryAddClauseFromCString("alpha x beta y | ! gamma x delta y");
	ASSERT_INT32_EQUAL(numberOfStaleServices(alphaBetaRelation), 3)

	// Add rule deriving relation (mu nu) from (alpha beta)
	DictionaryEntry gRule = DictionaryAddClauseFromCString("mu x nu y | ! alpha x beta y");
	ASSERT_INT32_EQUAL(numberOfStaleServices(alphaBetaRelation), 3)

	// Query (mu nu) while (alpha beta) is still stale. This should recompile the
	// (alpha >ID beta >ID) service first, as it was marked stale
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("mu x nu y"), 2)
	// One less stale service
	ASSERT_INT32_EQUAL(numberOfStaleServices(alphaBetaRelation), 2)

	DictionaryRemoveClause(&gRule);
	DictionaryRemoveClause(&aRule);
	RelationRemoveTuple(alphaBetaRelation, TypedTuplePeekAtoms(FormulaGetActors(alphaBetaFact)), 0);
	RelationRemoveTuple(gammaDeltaRelation, TypedTuplePeekAtoms(FormulaGetActors(gammaDeltaFact)), 0);
	DropRelation(alphaBetaRelation);
	DropRelation(gammaDeltaRelation);
	ReleaseFormula(alphaBetaFact);
	ReleaseFormula(gammaDeltaFact);
}


/**
 * Test compiling a relation with both primitive and compiled services to a JOIN of
 * UNION services.
 */
void testSelfJoinOverUnionRelation(void)
{
	// Relation (alpha beta) stores the edges (A, B) and (B, C)
	Atom edge1 = CStringToTerm("alpha 'A beta 'B");
	Relation alphaBetaRelation = RelationFromFact(FormulaGetView(edge1));
	TupleStore * alphaBetaStore = CreateTupleStore(alphaBetaRelation, &btreeStorageProvider, 2, 0);
	TupleStoreAddTuple(alphaBetaStore, TypedTuplePeekAtoms(FormulaGetActors(edge1)), 0);
	Atom edge2 = CStringToTerm("alpha 'B beta 'C");
	TupleStoreAddTuple(alphaBetaStore, TypedTuplePeekAtoms(FormulaGetActors(edge2)), 0);

	// Relation (gamma delta) stores the edge (C, D)
	Atom edge3 = CStringToTerm("gamma 'C delta 'D");
	Relation gammaDeltaRelation = RelationFromFact(FormulaGetView(edge3));
	TupleStore * gammaDeltaStore = CreateTupleStore(gammaDeltaRelation, &btreeStorageProvider, 2, 0);
	TupleStoreAddTuple(gammaDeltaStore, TypedTuplePeekAtoms(FormulaGetActors(edge3)), 0);

	// The below rule adds (C, D) to (alpha beta), its service is a UNION with tuples
	// (alpha A beta B)
	// (alpha B beta C)
	// (alpha C beta D)
	DictionaryEntry alphaRule = DictionaryAddClauseFromCString("alpha x beta y | ! gamma x delta y");
	// Adding the rule invalidates all 3 (alpha beta) services
	ASSERT_INT32_EQUAL(numberOfStaleServices(alphaBetaRelation), 3)
	// Running this query restores the (alpha >LETTER beta >LETTER) service
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("alpha x beta y"), 3)
	ASSERT_INT32_EQUAL(numberOfStaleServices(alphaBetaRelation), 2)


	// (mu nu) joins (alpha beta) with itself: the length-two paths a->c and b->d.
	// We expect a JOIN operator generating the tuples
	// (mu A nu C)
	// (mu B nu C)
	DictionaryEntry muRule = DictionaryAddClauseFromCString(
		"mu x nu y | ! alpha x beta z | ! alpha z beta y");
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("mu x nu y"), 2)
	ASSERT_INT32_EQUAL(numberOfStaleServices(alphaBetaRelation), 1)

	DictionaryRemoveClause(&muRule);
	DictionaryRemoveClause(&alphaRule);
	RelationRemoveTuple(alphaBetaRelation, TypedTuplePeekAtoms(FormulaGetActors(edge1)), 0);
	RelationRemoveTuple(alphaBetaRelation, TypedTuplePeekAtoms(FormulaGetActors(edge2)), 0);
	RelationRemoveTuple(gammaDeltaRelation, TypedTuplePeekAtoms(FormulaGetActors(edge3)), 0);
	DropRelation(alphaBetaRelation);
	DropRelation(gammaDeltaRelation);
	ReleaseFormula(edge1);
	ReleaseFormula(edge2);
	ReleaseFormula(edge3);
}


/**
 * CLAUDE: A stored relation created after a rule already derives its term form must have its
 * primitives marked stale, even though no compiled service was displaced (nothing to
 * invalidate). This is the case ClauseFormExistsForTermForm() catches; see CreateTupleStore().
 */
void testStorePrimitiveStaleWhenRuleExists(void)
{
	// (gamma delta) stores the edge (C, D)
	Atom gammaFact = CStringToTerm("gamma 'C delta 'D");
	Relation gammaDeltaRelation = RelationFromFact(FormulaGetView(gammaFact));
	TupleStore * gammaDeltaStore = CreateTupleStore(gammaDeltaRelation, &btreeStorageProvider, 2, 0);
	TupleStoreAddTuple(gammaDeltaStore, TypedTuplePeekAtoms(FormulaGetActors(gammaFact)), 0);

	// A rule deriving (alpha beta) from (gamma delta), added before any (alpha beta) relation
	// exists and before (alpha beta) is queried, so nothing is compiled or invalidated.
	DictionaryEntry alphaRule = DictionaryAddClauseFromCString("alpha x beta y | ! gamma x delta y");

	// Creating the (alpha beta) store with the edge (A, B). Its primitives must be stale
	// because the rule already derives the term form, although nothing was invalidated.
	Atom alphaFact = CStringToTerm("alpha 'A beta 'B");
	Relation alphaBetaRelation = RelationFromFact(FormulaGetView(alphaFact));
	TupleStore * alphaBetaStore = CreateTupleStore(alphaBetaRelation, &btreeStorageProvider, 2, 0);
	TupleStoreAddTuple(alphaBetaStore, TypedTuplePeekAtoms(FormulaGetActors(alphaFact)), 0);
	ASSERT_INT32_EQUAL(numberOfStaleServices(alphaBetaRelation), 3)

	// The query unions the stored edge (A, B) with the derived edge (C, D)
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("alpha x beta y"), 2)

	DictionaryRemoveClause(&alphaRule);
	RelationRemoveTuple(alphaBetaRelation, TypedTuplePeekAtoms(FormulaGetActors(alphaFact)), 0);
	RelationRemoveTuple(gammaDeltaRelation, TypedTuplePeekAtoms(FormulaGetActors(gammaFact)), 0);
	DropRelation(alphaBetaRelation);
	DropRelation(gammaDeltaRelation);
	ReleaseFormula(alphaFact);
	ReleaseFormula(gammaFact);
}


static RelationFixture edgeFixture;


/**
 * CLAUDE: A conjunction query is compiled to a JOIN of its terms, as a rule body is. The
 * edge relation has the edges a to b, a to a, b to b and b to c, and the query below asks
 * for every walk of two edges. The variable y repeats across the two terms, so the
 * compiled service repeats a parameter. Asking again re-uses the compiled service.
 */
void testQueryConjunction(void)
{
	size32 nServicesBefore = NumberOfServices();
	SetupEdgeFixture(&edgeFixture);
	size32 nServices = NumberOfServices();

	// The second term reads the edges from a given node. The edge relation has no service
	// binding only the from role, so a FILTER service is compiled for it as well.
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("edge d from x to y & edge f from y to z"), 6)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 2)
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("edge d from x to y & edge f from y to z"), 6)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 2)

	// A constant binds the first term; the walks from a are a-b-b, a-b-c, a-a-b and a-a-a.
	// Both terms read the FILTER service compiled above.
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("edge d from \"a\" to y & edge f from y to z"), 4)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 3)

	// No walk of two edges leads back to its start, except by a self edge
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("edge d from x to y & edge f from y to x"), 2)

	// Dropping the edge relation removes the compiled services reading it
	TeardownRelationFixture(&edgeFixture);
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServicesBefore)
}


/**
 * CLAUDE: A term of a conjunction query may be answered by a rule, which is compiled when
 * the conjunction is. Adding a rule for a term form of the conjunction invalidates the
 * compiled conjunction service.
 */
void testQueryConjunctionRule(void)
{
	SetupEdgeFixture(&edgeFixture);
	DictionaryEntry entry = DictionaryAddClauseFromCString("reach x to y | ! edge e from x to y");
	size32 nServices = NumberOfServices();

	// reach has the pairs a-b, a-a, b-b and b-c, followed by an edge from b, a, b and c
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("reach x to y & edge f from y to z"), 6)
	// the conjunction service, the (reach to) service it reads, and a FILTER service
	// the (reach to) service reads
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 3)

	// The FILTER service reads only the edge relation, and remains
	DictionaryRemoveClause(&entry);
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 1)

	TeardownRelationFixture(&edgeFixture);
}


int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testQueryStoredFacts);
	ExecuteTest(testQueryIntegerLiteral);
	ExecuteTest(testQueryCompilesOnce);
	ExecuteTest(testQueryParameterIO);
	ExecuteTest(testQueryRepeatedVariable);
	ExecuteTest(testQueryCompileIgnoresRepeatedVariable);
	ExecuteTest(testQueryWithoutAnswer);
	ExecuteTest(testInvalidateServiceByNewRelation);
	ExecuteTest(testInvalidateServiceByRule);
	ExecuteTest(testInvalidateRelationByRule);
	ExecuteTest(testCompileClearsStaleRelation);
	ExecuteTest(testStaleClearedAfterRuleRemoved);
	ExecuteTest(testStaleBodyTermUsesPrimitiveOnly);
	ExecuteTest(testSelfJoinOverUnionRelation);
	ExecuteTest(testStorePrimitiveStaleWhenRuleExists);
	ExecuteTest(testQueryConjunction);
	ExecuteTest(testQueryConjunctionRule);

	UnloadLibraries();
	KernelShutdown();
	TestSummary();
}
