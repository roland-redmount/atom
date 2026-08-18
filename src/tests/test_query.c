
#include "kernel/compiler.h"
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/kernel.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "library/MachineService.h"
#include "parser/TermBuilder.h"
#include "testing/fixtures.h"
#include "testing/testing.h"
#include "ui/query.h"


static RelationFixture precSuccFixture;


/**
 * Remove every service compiled for the given query, and the relations they belong to.
 * A test that compiles a query must leave the registries as it found them.
 */
static void removeQueryServices(char const * queryString)
{
	Formula * query = CStringToTerm(queryString);
	size8 arity = query->actors->nAtoms;
	TypedTuple * parameters = CreateTypedTuple(arity);
	GetQueryParameters(query->actors, parameters);

	Service service;
	index8 permutation[arity];
	while(DispatchQuery(query->form, parameters, &service, permutation)) {
		ServiceRegistryRemoveAll(service.relation);
		RelationRegistryRemove(service.relation);
	}
	FreeTypedTuple(parameters);
	FreeFormula(query);
}


/**
 * Number of tuples the given query is answered with
 */
static size32 countQueryTuples(char const * queryString)
{
	Formula * query = CStringToTerm(queryString);
	MixedTypeRelation * relation = UserQuery(query);
	size32 nTuples = 0;
	while(MixedTypeRelationNext(relation))
		nTuples++;
	FreeMixedTypeRelation(relation);
	FreeFormula(query);
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

	ASSERT_UINT32_EQUAL(countQueryTuples("prec _x succ _y"), PREC_SUCC_N_EDGES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices)

	TeardownRelationFixture(&precSuccFixture);
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
	ASSERT_UINT32_EQUAL(countQueryTuples("before _x after _y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

	// The same query is answered by that service, and compiles nothing further
	ASSERT_UINT32_EQUAL(countQueryTuples("before _x after _y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

	removeQueryServices("before _x after _y");
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	TeardownRelationFixture(&precSuccFixture);
}


/**
 * Two queries of the same term form are of the same "type" only if they agree on the direction
 * and input type of every parameter. Here, the query (before x after y) is distinct
 * from (before "a" after y) since the parameter IO direction differs, so they compile
 * independently to two distinct services.
 */
void testQueryTypeIsParameterDirections(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	AddTransitiveClosureRules(&entry1, &entry2);
	size32 nServices = ServiceRegistryCount();

	ASSERT_UINT32_EQUAL(countQueryTuples("before _x after _y"), PREC_SUCC_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

	// b, c and d come after a, and this query type compiles a service of its own
	ASSERT_UINT32_EQUAL(countQueryTuples("before \"a\" after _y"), 3)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 2)
	ASSERT_UINT32_EQUAL(countQueryTuples("before \"a\" after _y"), 3)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 2)

	removeQueryServices("before _x after _y");
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	TeardownRelationFixture(&precSuccFixture);
}


/**
 * A repeated variable is not part of the query type: the query is compiled and dispatched
 * with distinct parameters, and the equality constraint is applied to the tuples as they are read.
 * The query (list "ab" position x element x) is the case that tells the two apart, as it
 * matches no service while a service of its type exists.
 */
void testQueryRepeatedVariable(void)
{
	SetupPrecSuccFixture(&precSuccFixture);
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	AddTransitiveClosureRules(&entry1, &entry2);
	size32 nServices = ServiceRegistryCount();

	// b and c lie on a cycle, and so come after themselves
	ASSERT_UINT32_EQUAL(countQueryTuples("before _x after _x"), 2)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

	// A position is never a letter, so this query yields no tuples. Compiling it would
	// register the (list position element) service of the kernel a second time.
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" position _x element _x"), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

	removeQueryServices("before _x after _y");
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	TeardownRelationFixture(&precSuccFixture);
}


/**
 * A query that no fact and no rule answers has no tuples, and compiles nothing. It is
 * compiled again every time it is asked, which is what lets it start working once a rule
 * answering it is asserted.
 */
void testQueryWithoutAnswer(void)
{
	size32 nServices = ServiceRegistryCount();

	ASSERT_UINT32_EQUAL(countQueryTuples("nowhere _x nothing _y"), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices)
	ASSERT_UINT32_EQUAL(countQueryTuples("nowhere _x nothing _y"), 0)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices)
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testQueryStoredFacts);
	ExecuteTest(testQueryCompilesOnce);
	ExecuteTest(testQueryTypeIsParameterDirections);
	ExecuteTest(testQueryRepeatedVariable);
	ExecuteTest(testQueryWithoutAnswer);

	FreeMachineServices();
	KernelShutdown();
	TestSummary();
}
