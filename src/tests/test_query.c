
#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/formula.h"
#include "storage/RelationBTree.h"
#include "library/MachineService.h"
#include "parser/TermBuilder.h"
#include "testing/fixtures.h"
#include "library/list.h"
#include "library/string.h"
#include "testing/testing.h"
#include "ui/query.h"


static RelationFixture precSuccFixture;


/**
 * Run a UserQuery (possibly compiling the query) and count the number of tuples
 * in the resulting relations
 */
static size32 runQueryAndCountTuples(char const * queryString)
{
	Atom query = CStringToTerm(queryString);
	MixedTypeRelation * relation = UserQuery(query);
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

	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("prec x succ y"), PREC_SUCC_N_EDGES)
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
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("list \"ab\" position 1 element e"), 1)
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("list \"ab\" position 2 element e"), 1)
	// a position no element of the list has
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("list \"ab\" position 3 element e"), 0)
	// the length of the list, whose service takes an INT column too
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("list \"ab\" length 2"), 1)
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
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 1)

	// The same query is answered by that service, and compiles nothing further
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
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

	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 1)

	// b, c and d come after a, and this query compiles a service of its own
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("before \"a\" after y"), 3)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 2)
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("before \"a\" after y"), 3)
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
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("before x after x"), 2)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 1)

	// This query yields no tuples, since the (list position element) service
	// has distinct parameter types for the position and element roles, and
	// therefore all tuples from the servuce will fail the equality constraint.
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("list \"ab\" position x element x"), 0)
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
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("item e index p"), 2)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices + 2)

	// This query re-uses the above compiled services, but yields no tuples
	// since the element type is never an INT.
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("item z index z"), 0)
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

	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("nowhere x nothing y"), 0)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices)
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("nowhere x nothing y"), 0)
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
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 1)

	// Add a second RelationTable of the (prec succ) form, creating new primitive services
	// not present during the compilation above. This invalidates the compiled service
	// since we now may have additional facts.
	// NOTE: this is overly conservative: the compiled service actually does not depend
	// on this new relation, since its type signature differs from the service found during compilation.
	Relation const * intRelation = CreateRelation(
		precSuccFixture.termForm, 2, CreateTypeSignature((byte[]) {AT_ID, AT_INT}, 2));
	RelationTable * intTable = CreateRelationTable(
		intRelation, &btreeStorageProvider, (index8[]) {0, 1});
	ReleaseRelation(intRelation);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)

	// Asking again compiles the query again, yielding a new (before after) relation.
	// Since the (prec:ID succ:INT) relation was empty, there are no additional tuples.
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 2)

	// Cleanup
	RemoveAllCompiledServices();
	ReleaseRelationTable(intTable);
	TeardownRelationFixture(&precSuccFixture);
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
}


/**
 * A rule asserted after a query of its form was compiled has to reach that query, so
 * adding or removing a rule removes the compiled services of every term form the rule
 * mentions. Here the recursive rule turns the edges of the graph into its closure.
 */
void testQueryInvalidatedByRule(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry baseEntry = DictionaryAddClauseFromCString(
		"before x after y | ! prec x succ y");
	size32 nServices = NumberOfServices();

	// With the base rule alone, the derived relation is the edge relation itself
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("before x after y"), PREC_SUCC_N_EDGES)
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 1)

	// The recursive rule makes the same query a different question
	DictionaryEntry recursiveEntry = DictionaryAddClauseFromCString(
		"before x after y | ! prec x succ z | ! before z after y");
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 1)

	// And removing it again makes it the first question once more
	DictionaryRemoveClause(&recursiveEntry);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	ASSERT_UINT32_EQUAL(runQueryAndCountTuples("before x after y"), PREC_SUCC_N_EDGES)

	DictionaryRemoveClause(&baseEntry);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), 0)
	ASSERT_UINT32_EQUAL(NumberOfServices(), nServices)
	TeardownRelationFixture(&precSuccFixture);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	ListSetup();
	StringSetup();

	ExecuteTest(testQueryStoredFacts);
	ExecuteTest(testQueryIntegerLiteral);
	ExecuteTest(testQueryCompilesOnce);
	ExecuteTest(testQueryParameterIO);
	ExecuteTest(testQueryRepeatedVariable);
	ExecuteTest(testQueryCompileIgnoresRepeatedVariable);
	ExecuteTest(testQueryWithoutAnswer);
	ExecuteTest(testInvalidateServiceByNewRelation);
	ExecuteTest(testQueryInvalidatedByRule);

	FreeMachineServices();
	StringShutdown();
	ListShutdown();
	KernelShutdown();
	TestSummary();
}
