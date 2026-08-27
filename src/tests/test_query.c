
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
 * Number of tuples the given query is answered with
 */
static size32 countQueryTuples(char const * queryString)
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
	size32 nServices = ServiceRegistryCount();

	ASSERT_UINT32_EQUAL(countQueryTuples("prec x succ y"), PREC_SUCC_N_EDGES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices)

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
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" position 1 element e"), 1)
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" position 2 element e"), 1)
	// a position no element of the list has
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" position 3 element e"), 0)
	// the length of the list, whose service takes an INT column too
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" length 2"), 1)
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
	size32 nServices = ServiceRegistryCount();

	// The first query compiles the service deriving the closure
	ASSERT_UINT32_EQUAL(countQueryTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

	// The same query is answered by that service, and compiles nothing further
	ASSERT_UINT32_EQUAL(countQueryTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

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
	size32 nServices = ServiceRegistryCount();

	ASSERT_UINT32_EQUAL(countQueryTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

	// b, c and d come after a, and this query compiles a service of its own
	ASSERT_UINT32_EQUAL(countQueryTuples("before \"a\" after y"), 3)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 2)
	ASSERT_UINT32_EQUAL(countQueryTuples("before \"a\" after y"), 3)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 2)

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
	size32 nServices = ServiceRegistryCount();

	// b and c lie on a cycle, and so come after themselves
	ASSERT_UINT32_EQUAL(countQueryTuples("before x after x"), 2)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

	// This query yields no tuples, since the (list position element) service
	// has distinct parameter types for the position and element roles, and
	// therefore all tuples from the servuce will fail the equality constraint.
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" position x element x"), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

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
	size32 nServices = ServiceRegistryCount();

	// This query compiles two services since we have two (list position element)
	// services with element types AT_ID and AT_NAME; it yields the letters of "ab"
	// and their positions.
	ASSERT_UINT32_EQUAL(countQueryTuples("item e index p"), 2)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 2)

	// This query re-uses the above compiled services, but yields no tuples
	// since the element type is never an INT.
	ASSERT_UINT32_EQUAL(countQueryTuples("item z index z"), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 2)

	DictionaryRemoveClause(&entry);
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices)
}


/**
 * A query that no fact and no rule answers has no tuples, and compiles nothing. It is
 * compiled again every time it is asked, which is what lets it start working once a rule
 * answering it is asserted.
 */
void testQueryWithoutAnswer(void)
{
	size32 nServices = ServiceRegistryCount();

	ASSERT_UINT32_EQUAL(countQueryTuples("nowhere x nothing y"), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices)
	ASSERT_UINT32_EQUAL(countQueryTuples("nowhere x nothing y"), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices)
}


/**
 * A compiled service is a cache over the knowledge base. A relation of the same term form
 * appearing gives a query one more relation to match, so the service is removed and the
 * next query compiles it again; removing the relation it was compiled from removes it for
 * good.
 */
void testQueryInvalidatedByRelation(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	AddTransitiveClosureRules(&entry1, &entry2);
	size32 nServices = ServiceRegistryCount();

	ASSERT_UINT32_EQUAL(countQueryTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)

	// A second relation of the (prec succ) form, whose services the compiled one knows
	// nothing of
	Relation const * intRelation = CreateRelation(
		precSuccFixture.termForm, 2, CreateTypeSignature((byte[]) {AT_ID, AT_INT}, 2));
	RelationTable * intTable = CreateRelationTable(
		intRelation, &btreeTableProvider, (index8[]) {0, 1});
	ReleaseRelation(intRelation);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)

	// Asking again compiles the query anew, over both relations, and the new one holds
	// no tuples to add
	ASSERT_UINT32_EQUAL(countQueryTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_TRUE(ServiceRegistryNCompiled() > 0)

	// Removing that relation again takes the service compiled over it, and only that
	// one: the service over the remaining relation still answers what it always did
	DropRelationTable(intTable);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)
	ASSERT_UINT32_EQUAL(countQueryTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)

	// Removing the relation the service was compiled from takes the service with it,
	// and the computed relation it answered
	TeardownRelationFixture(&precSuccFixture);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices - PREC_SUCC_N_SERVICES)

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
	size32 nServices = ServiceRegistryCount();

	// With the base rule alone, the derived relation is the edge relation itself
	ASSERT_UINT32_EQUAL(countQueryTuples("before x after y"), PREC_SUCC_N_EDGES)
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)

	// The recursive rule makes the same query a different question
	DictionaryEntry recursiveEntry = DictionaryAddClauseFromCString(
		"before x after y | ! prec x succ z | ! before z after y");
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_UINT32_EQUAL(countQueryTuples("before x after y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 1)

	// And removing it again makes it the first question once more
	DictionaryRemoveClause(&recursiveEntry);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_UINT32_EQUAL(countQueryTuples("before x after y"), PREC_SUCC_N_EDGES)

	DictionaryRemoveClause(&baseEntry);
	ASSERT_UINT32_EQUAL(ServiceRegistryNCompiled(), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices)
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
	ExecuteTest(testQueryInvalidatedByRelation);
	ExecuteTest(testQueryInvalidatedByRule);

	FreeMachineServices();
	StringShutdown();
	ListShutdown();
	KernelShutdown();
	TestSummary();
}
