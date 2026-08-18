
#include "kernel/compiler.h"
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/MachineService.h"
#include "parser/ClauseBuilder.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"
#include "ui/query.h"


/**
 * A directed graph (prec succ) of two components, one of which has the cycle b -> c -> b:
 *
 *   a -> b -> c -> d,  c -> b,  e -> f
 *
 * The transitive closure of the graph is the relation the rules below derive.
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
	setupPrecSuccFixture();
	size32 nServices = ServiceRegistryCount();

	ASSERT_UINT32_EQUAL(countQueryTuples("prec _x succ _y"), TEST_N_PREC_SUCC_EDGES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices)

	teardownPrecSuccFixture();
}


/**
 * A query no service answers is compiled when it is first asked, and answered by the
 * compiled services from then on. The rules here derive the transitive closure of the
 * graph, so the answer holds tuples that are not facts of any stored relation.
 */
void testQueryCompilesOnce(void)
{
	setupPrecSuccFixture();
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	addTransitiveClosureRules(&entry1, &entry2);
	size32 nServices = ServiceRegistryCount();

	// The first query compiles the service deriving the closure
	ASSERT_UINT32_EQUAL(countQueryTuples("before _x after _y"), TEST_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

	// The same query is answered by that service, and compiles nothing further
	ASSERT_UINT32_EQUAL(countQueryTuples("before _x after _y"), TEST_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

	removeQueryServices("before _x after _y");
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	teardownPrecSuccFixture();
}


/**
 * Two queries of the same term form are of the same "type" only if they agree on the direction
 * and input type of every parameter. Here, the query (before x after y) is distinct
 * from (before "a" after y) since the parameter IO direction differs, so they compile
 * independently to two distinct services.
 */
void testQueryTypeIsParameterDirections(void)
{
	setupPrecSuccFixture();
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	addTransitiveClosureRules(&entry1, &entry2);
	size32 nServices = ServiceRegistryCount();

	ASSERT_UINT32_EQUAL(countQueryTuples("before _x after _y"), TEST_N_CLOSURE_TUPLES)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 1)

	// b, c and d come after a, and this query type compiles a service of its own
	ASSERT_UINT32_EQUAL(countQueryTuples("before \"a\" after _y"), 3)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 2)
	ASSERT_UINT32_EQUAL(countQueryTuples("before \"a\" after _y"), 3)
	ASSERT_UINT32_EQUAL(ServiceRegistryCount(), nServices + 2)

	removeQueryServices("before _x after _y");
	DictionaryRemoveClause(&entry2);
	DictionaryRemoveClause(&entry1);
	teardownPrecSuccFixture();
}


/**
 * A repeated variable is not part of the query type: the query is compiled and dispatched
 * with distinct parameters, and the equality constraint is applied to the tuples as they are read.
 * The query (list "ab" position x element x) is the case that tells the two apart, as it
 * matches no service while a service of its type exists.
 */
void testQueryRepeatedVariable(void)
{
	setupPrecSuccFixture();
	DictionaryEntry entry1;
	DictionaryEntry entry2;
	addTransitiveClosureRules(&entry1, &entry2);
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
	teardownPrecSuccFixture();
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
