
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
	Atom query = CStringToTerm(queryString);
	MixedTypeRelation * relation = UserQuery(FormulaGetView(query));
	size32 nTuples = 0;
	while(MixedTypeRelationNext(relation))
		nTuples++;
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
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("before x after x"), 2)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 1)

	// This query yields no tuples, since the (list position element) service
	// has distinct parameter types for the position and element roles, and
	// therefore all tuples from the servuce will fail the equality constraint.
	ASSERT_UINT32_EQUAL(runUserQueryAndCountTuples("list \"ab\" position x element x"), 0)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 1)

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
	// This should invalidate the service created by SetupPrecSuccFixture(), since it now
	// depends on the new relation.
	Relation intRelation = {
		.termForm = precSuccFixture.termForm,
		.typeSignature = CreateTypeSignature((byte[]) {AT_ID, AT_INT}, 2)
	};
	CreateTupleStore(intRelation, &btreeStorageProvider, 2, 0);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)

	// Querying again compiles an additional service for the (before:ID after:INT) relation.
	// Since the (prec:ID succ:INT) relation was empty, there are no additional tuples.
	// TODO: this does not work. Since the service is not invalidated above, re-compilation is
	// not triggered, and we do not discover the additional type variant.
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
	index8 facultyColumn = PredicateRoleIndex(TermFormGetPredicateForm(relation.termForm), faculty);
	NameRelease(faculty);

	// Adding the recursive rule should mark the relation as stale
	// NOTE: A new rule must invalidate a primitive relation even if no
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"number n faculty f | ! < n > 0 | ! + m + 1 = n | ! number m faculty e | ! * e * n = f");
	ASSERT_TRUE(RelationIsStale(relation))

	// Run a query
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

	UnloadLibraries();
	KernelShutdown();
	TestSummary();
}
