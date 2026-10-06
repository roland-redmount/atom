#include "kernel/dictionary.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/MixedTypeRelation.h"
#include "kernel/operator.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/TupleStore.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/library.h"
#include "library/string.h"
#include "parser/TermBuilder.h"
#include "storage/RelationBTree.h"
#include "testing/fixtures.h"
#include "testing/testing.h"
#include "ui/assert.h"
#include "ui/query.h"


/**
 * The relation (circle ID radius FLOAT) with an IFACT service
 * (circle >ID radius <FLOAT), and the column index of each role.
 */
typedef struct s_CircleFixture {
	Atom termForm;
	Relation relation;
	TupleStore * store;
	index8 circleIndex;
	index8 radiusIndex;
	Service ifactService;
	// The service reading the store with the ID column as the only input
	Service byIdService;
} CircleFixture;


static index8 findRoleIndex(Atom termForm, char const * roleName)
{
	Atom role = CreateNameFromCString(roleName);
	index8 index = PredicateRoleIndex(TermFormGetPredicateForm(termForm), role);
	NameRelease(role);
	return index;
}


static Service createCircleService(CircleFixture const * fixture, byte circleIO, byte radiusIO)
{
	byte parameterIO[2];
	parameterIO[fixture->circleIndex] = circleIO;
	parameterIO[fixture->radiusIndex] = radiusIO;
	return (Service) {
		.relation = fixture->relation,
		.ioSignature = CreateIOSignature(parameterIO, 2)
	};
}


static void setupCircleFixture(CircleFixture * fixture)
{
	fixture->termForm = CreateTermFormFromRoleNames((char const * []) {"circle", "radius"}, 2, true);
	fixture->circleIndex = findRoleIndex(fixture->termForm, "circle");
	fixture->radiusIndex = findRoleIndex(fixture->termForm, "radius");

	byte atomTypes[2];
	atomTypes[fixture->circleIndex] = AT_ID;
	atomTypes[fixture->radiusIndex] = AT_FLOAT;
	fixture->relation = (Relation) {
		.form = fixture->termForm,
		.typeSignature = CreateTypeSignature(atomTypes, 2)
	};
	// The ID column comes first in the index order, as CreateIFactOperator() requires
	index8 indexColumns[2] = {fixture->circleIndex, fixture->radiusIndex};
	fixture->store = CreateTupleStore(fixture->relation, GetStorageProvider(PROVIDER_BTREE), 2, indexColumns);

	fixture->ifactService = createCircleService(fixture, PARAMETER_OUT, PARAMETER_IN);
	fixture->byIdService = createCircleService(fixture, PARAMETER_IN, PARAMETER_OUT);
	CreateService(fixture->ifactService, CreateIFactOperator(fixture->store, fixture->circleIndex));
}


/**
 * Remove the service reading the store by ID, which also removes the IFACT service
 * depending on it, then drop the relation.
 */
static void teardownCircleFixture(CircleFixture * fixture)
{
	// CLAUDE: The IFACT operator of the fixture is not compiled from an ifact rule,
	// so its cached ifacts are released here
	IFactReleaseCached(fixture->store, fixture->circleIndex);
	RemoveService(fixture->byIdService);
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(fixture->store), 0)
	ASSERT_TRUE(ServiceGetOperator(fixture->ifactService) == 0)
	DropRelation(fixture->relation);
	IFactRelease(fixture->termForm);
}


/**
 * Call the IFACT operator for the given radius and return the circle ID.
 */
static Atom callCircleIFact(CircleFixture const * fixture, double radius)
{
	Atom arguments[2];
	arguments[fixture->circleIndex] = (Atom) {0};
	arguments[fixture->radiusIndex] = (Atom) {._float = radius};
	ASSERT_TRUE(OperatorCallOnce(ServiceGetOperator(fixture->ifactService), arguments))
	ASSERT_TRUE(arguments[fixture->radiusIndex]._float == radius)
	return arguments[fixture->circleIndex];
}


void testIFactOperator(void)
{
	size32 nIFacts = IFactTotalCount();
	size32 nCompiledServices = NumberOfCompiledServices();
	CircleFixture fixture;
	setupCircleFixture(&fixture);
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), nCompiledServices + 1)
	// The fixture holds the term form and predicate form ifacts
	size32 nFixtureIFacts = IFactTotalCount();

	// The first operator call creates the ifact and its tuple
	Atom circle5 = callCircleIFact(&fixture, 5.0);
	ASSERT_TRUE(circle5.hash != 0)
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(fixture.store), 1)
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nFixtureIFacts + 1)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(circle5), 1)

	// A second call yields the existing ifact, and the cache still holds one reference
	ASSERT_TRUE(SameAtoms(callCircleIFact(&fixture, 5.0), circle5))
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(fixture.store), 1)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(circle5), 1)

	// Another radius yields another ifact
	Atom circle6 = callCircleIFact(&fixture, 6.0);
	ASSERT_FALSE(SameAtoms(circle6, circle5))
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(fixture.store), 2)
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nFixtureIFacts + 2)

	// A cached tuple cannot be retracted
	TypedAtom actors[2];
	actors[fixture.circleIndex] = CreateTypedAtom(AT_ID, circle5);
	actors[fixture.radiusIndex] = CreateTypedAtom(AT_FLOAT, (Atom) {._float = 5.0});
	Atom fact = CreateFormulaFromArray(fixture.termForm, actors);
	RetractFact(FormulaGetView(fact));
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(fixture.store), 2)
	ReleaseFormula(fact);
	ASSERT_UINT32_EQUAL(IFactReferenceCount(circle5), 1)

	teardownCircleFixture(&fixture);
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nIFacts)
	ASSERT_UINT32_EQUAL(NumberOfCompiledServices(), nCompiledServices)
}


/**
 * Run a UserQuery(), count the resulting tuples, and return
 * the last tuple in *lastTuple, which the caller must free.
 */
static size32 runQuery(Atom query, TypedTuple ** lastTuple)
{
	MixedTypeRelation * relation = UserQuery(FormulaGetView(query));
	size32 nTuples = 0;
	*lastTuple = 0;
	while(MixedTypeRelationNext(relation)) {
		if(*lastTuple)
			FreeTypedTuple(*lastTuple);
		*lastTuple = CreateTupleFromTuple(MixedTypeRelationPeekTuple(relation));
		nTuples++;
	}
	FreeMixedTypeRelation(relation);
	return nTuples;
}


/**
 * CLAUDE: Run a query and count the tuples having the given atom at the given index.
 * Returns the total number of tuples.
 */
static size32 runQueryCountAtom(Atom query, index8 index, Atom atom, size32 * nMatching)
{
	MixedTypeRelation * relation = UserQuery(FormulaGetView(query));
	size32 nTuples = 0;
	*nMatching = 0;
	while(MixedTypeRelationNext(relation)) {
		TypedTuple const * tuple = MixedTypeRelationPeekTuple(relation);
		if(SameAtoms(TypedTupleGetElement(tuple, index).atom, atom))
			(*nMatching)++;
		nTuples++;
	}
	FreeMixedTypeRelation(relation);
	return nTuples;
}


void testIFactQuery(void)
{
	CircleFixture fixture;
	setupCircleFixture(&fixture);

	// Query the IFACT service for the circle of radius 5.0, creating an ifact
	Atom query = CStringToTerm("circle x radius 5.0");
	TypedTuple * tuple;
	ASSERT_UINT32_EQUAL(runQuery(query, &tuple), 1)
	ReleaseFormula(query);
	TypedAtom circle = TypedTupleGetElement(tuple, fixture.circleIndex);
	ASSERT_UINT32_EQUAL(circle.type, AT_ID)
	FreeTypedTuple(tuple);
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(fixture.store), 1)

	// Query the cached tuple by its ID, with the service (circle <ID radius >FLOAT)
	Atom variablesQuery = CStringToTerm("circle x radius r");
	FormulaView variablesView = FormulaGetView(variablesQuery);
	TypedAtom actors[2];
	for(index8 i = 0; i < 2; i++)
		actors[i] = TypedTupleGetElement(variablesView.actors, i);
	actors[fixture.circleIndex] = circle;
	Atom byIdQuery = CreateFormulaFromArray(FormulaGetForm(variablesQuery), actors);
	ReleaseFormula(variablesQuery);

	ASSERT_UINT32_EQUAL(runQuery(byIdQuery, &tuple), 1)
	ReleaseFormula(byIdQuery);
	TypedAtom radius = TypedTupleGetElement(tuple, fixture.radiusIndex);
	ASSERT_UINT32_EQUAL(radius.type, AT_FLOAT)
	ASSERT_TRUE(radius.atom._float == 5.0)
	ASSERT_TRUE(SameTypedAtoms(TypedTupleGetElement(tuple, fixture.circleIndex), circle))
	FreeTypedTuple(tuple);
	// Reading the cached tuple creates no new tuple
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(fixture.store), 1)

	teardownCircleFixture(&fixture);
}


/**
 * Adding an ifact rule invalidates services of its term form, as with adding a clause.
 * Compiled services are removed, including an IFACT service, and primitive services are
 * marked stale, also for a relation created after the rule.
 */
void testIFactRuleInvalidation(void)
{
	// Create a fixture with a B-tree storage and a service using an IFACT operator
	CircleFixture fixture;
	setupCircleFixture(&fixture);
	// Evaluate the IFACT operator, caching a tuple
	callCircleIFact(&fixture, 5.0);
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(fixture.store), 1)

	// Add a stored tuple (circle "c" radios 7.0)
	Atom circle = CreateStringFromCString("c");
	Atom tuple[2];
	tuple[fixture.circleIndex] = circle;
	tuple[fixture.radiusIndex] = (Atom) {._float = 7.0};
	ASSERT_UINT32_EQUAL(TupleStoreAddTuple(fixture.store, tuple, 0), TUPLE_ADDED)
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(fixture.store), 2)

	// Add the ifact rule (circle * radius r), invalidating existing services
	Atom rule = CStringToTerm("circle * radius r");
	FormulaView ifactRule = DictionaryAddIFactRule(rule);
	ReleaseFormula(rule);

	// The existing IFACT service is removed
	ASSERT_TRUE(ServiceGetOperator(fixture.ifactService) == 0)
	// the cached tuple is kept; normally released by DictionaryRemoveIFactRule()
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(fixture.store), 2)
	// The primitive service from the B-tree provider is marked stale
	ASSERT_TRUE(ServiceIsStale(fixture.byIdService))

	// A query on the relation still yields the stored fact,
	// by marking the pritmitive service non-stale
	Atom query = CStringToTerm("circle x radius r");
	// CLAUDE: the query also yields the cached tuple
	size32 nStored;
	ASSERT_UINT32_EQUAL(runQueryCountAtom(query, fixture.circleIndex, circle, &nStored), 2)
	ASSERT_UINT32_EQUAL(nStored, 1)
	ReleaseFormula(query);

	// A relation of the term form created after the rule has stale primitive services
	Relation intRelation = {
		.form = fixture.termForm,
		.typeSignature = fixture.relation.typeSignature
	};
	intRelation.typeSignature.atomTypes[fixture.radiusIndex] = AT_INT;
	TupleStore * intStore = CreateTupleStore(intRelation, GetStorageProvider(PROVIDER_BTREE), 2, 0);
	byte parameterIO[2] = {PARAMETER_OUT, PARAMETER_OUT};
	Service intService = {.relation = intRelation, .ioSignature = CreateIOSignature(parameterIO, 2)};
	ASSERT_TRUE(ServiceIsStale(intService))
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(intStore), 0)
	DropRelation(intRelation);

	DictionaryRemoveIFactRule(&ifactRule);
	// CLAUDE: removing the rule releases the cached tuple
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(fixture.store), 1)
	ASSERT_UINT32_EQUAL(TupleStoreRemoveTuple(fixture.store, tuple, 0), TUPLE_REMOVED)
	IFactRelease(circle);
	teardownCircleFixture(&fixture);
}


/**
 * CLAUDE: The term form (circle radius) with the column index of each role, and the
 * relation (circle ID radius FLOAT), for tests compiling the ifact rule (circle * radius r).
 */
typedef struct s_CircleRule {
	Atom termForm;
	index8 circleIndex;
	index8 radiusIndex;
	Relation relation;
	FormulaView ifactRule;
} CircleRule;


static void setupCircleRule(CircleRule * circleRule)
{
	circleRule->termForm = CreateTermFormFromRoleNames((char const * []) {"circle", "radius"}, 2, true);
	circleRule->circleIndex = findRoleIndex(circleRule->termForm, "circle");
	circleRule->radiusIndex = findRoleIndex(circleRule->termForm, "radius");
	byte atomTypes[2];
	atomTypes[circleRule->circleIndex] = AT_ID;
	atomTypes[circleRule->radiusIndex] = AT_FLOAT;
	circleRule->relation = (Relation) {
		.form = circleRule->termForm,
		.typeSignature = CreateTypeSignature(atomTypes, 2)
	};
	Atom rule = CStringToTerm("circle * radius r");
	circleRule->ifactRule = DictionaryAddIFactRule(rule);
	ReleaseFormula(rule);
}


/**
 * CLAUDE: Drop the relation and release the term form. The caller must remove the ifact rule
 * first, which releases the cached ifacts.
 */
static void teardownCircleRule(CircleRule * circleRule)
{
	if(RelationExists(circleRule->relation))
		DropRelation(circleRule->relation);
	IFactRelease(circleRule->termForm);
}


static Service createCircleRuleService(CircleRule const * circleRule, byte circleIO, byte radiusIO)
{
	byte parameterIO[2];
	parameterIO[circleRule->circleIndex] = circleIO;
	parameterIO[circleRule->radiusIndex] = radiusIO;
	return (Service) {
		.relation = circleRule->relation,
		.ioSignature = CreateIOSignature(parameterIO, 2)
	};
}


/**
 * CLAUDE: Count the operators of the given type in an operator graph.
 */
static size32 countOperators(Operator const * op, enum OperatorType type)
{
	size32 count = (op->type == type) ? 1 : 0;
	for(index8 i = 0; i < OperatorNChildren(op); i++)
		count += countOperators(OperatorGetChild(op, i), type);
	return count;
}


/**
 * CLAUDE: The query (circle c radius 5.0) compiles to an IFACT operator from the ifact rule
 * (circle * radius r), creating the relation (circle ID radius FLOAT).
 */
void testCompileIFactRule(void)
{
	CircleRule circleRule;
	setupCircleRule(&circleRule);
	size32 nIFacts = IFactTotalCount();
	ASSERT_FALSE(RelationExists(circleRule.relation))

	Atom query = CStringToTerm("circle c radius 5.0");
	TypedTuple * tuple;
	ASSERT_UINT32_EQUAL(runQuery(query, &tuple), 1)
	TypedAtom circle = TypedTupleGetElement(tuple, circleRule.circleIndex);
	ASSERT_UINT32_EQUAL(circle.type, AT_ID)
	FreeTypedTuple(tuple);

	Service ifactService = createCircleRuleService(&circleRule, PARAMETER_OUT, PARAMETER_IN);
	ASSERT_UINT32_EQUAL(countOperators(ServiceGetOperator(ifactService), OPERATOR_IFACT), 1)
	TupleStore * store = RelationGetTupleStore(circleRule.relation);
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(store), 1)
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nIFacts + 1)

	// A second query yields the same circle
	ASSERT_UINT32_EQUAL(runQuery(query, &tuple), 1)
	ASSERT_TRUE(SameTypedAtoms(TypedTupleGetElement(tuple, circleRule.circleIndex), circle))
	FreeTypedTuple(tuple);
	ASSERT_UINT32_EQUAL(IFactReferenceCount(circle.atom), 1)
	ReleaseFormula(query);

	// Query the cached tuple by its ID
	Atom variablesQuery = CStringToTerm("circle c radius r");
	FormulaView variablesView = FormulaGetView(variablesQuery);
	TypedAtom actors[2];
	for(index8 i = 0; i < 2; i++)
		actors[i] = TypedTupleGetElement(variablesView.actors, i);
	actors[circleRule.circleIndex] = circle;
	Atom byIdQuery = CreateFormulaFromArray(FormulaGetForm(variablesQuery), actors);
	ASSERT_UINT32_EQUAL(runQuery(byIdQuery, &tuple), 1)
	ReleaseFormula(byIdQuery);
	TypedAtom radius = TypedTupleGetElement(tuple, circleRule.radiusIndex);
	ASSERT_TRUE(radius.atom._float == 5.0)
	FreeTypedTuple(tuple);

	// A query with the radius as output reads the stored tuples, with no IFACT operator
	ASSERT_UINT32_EQUAL(runQuery(variablesQuery, &tuple), 1)
	FreeTypedTuple(tuple);
	ReleaseFormula(variablesQuery);
	Service allOutputService = createCircleRuleService(&circleRule, PARAMETER_OUT, PARAMETER_OUT);
	ASSERT_UINT32_EQUAL(countOperators(ServiceGetOperator(allOutputService), OPERATOR_IFACT), 0)
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(store), 1)

	// Removing the rule removes the IFACT service and the cached ifact
	// A primitive service of the same signature, replaced by the IFACT service,
	// is restored; see ReplaceService()
	DictionaryRemoveIFactRule(&(circleRule.ifactRule));
	Operator const * ifactServiceOperator = ServiceGetOperator(ifactService);
	ASSERT_TRUE(!ifactServiceOperator || (ifactServiceOperator->type == OPERATOR_MACHINE))
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(store), 0)
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nIFacts)
	teardownCircleRule(&circleRule);
}


/**
 * Assert that the service is a UNION of an IFACT operator, as the first child,
 * and the primitive service that the service replaced; see ReplaceService().
 */
static void assertIFactUnion(Service service)
{
	ServiceRecord const * record = ServiceGetRecord(service);
	ASSERT_UINT32_EQUAL(record->op->type, OPERATOR_UNION)
	ASSERT_UINT32_EQUAL(record->op->impl._union.first->type, OPERATOR_IFACT)
	ASSERT_NOT_NULL(record->replacedOperator)
	ASSERT_PTR_EQUAL(record->op->impl._union.second, record->replacedOperator)
}


/**
 * CLAUDE: With both the ifact rule (circle * radius r) and the clause
 * (circle c radius r | ! disk c size r), the query (circle c radius 5.0) compiles to a UNION
 * of the IFACT operator and the clause, yielding the ifact circle and the disk "d".
 */
void testCompileIFactRuleWithClause(void)
{
	CircleRule circleRule;
	setupCircleRule(&circleRule);
	Atom diskFact = CStringToTerm("disk \"d\" size 5.0");
	ASSERT_INT32_EQUAL(AssertFormula(diskFact), ASSERT_OK)
	index8 diskIndex = findRoleIndex(FormulaGetForm(diskFact), "disk");
	Atom disk = TypedTupleGetElement(FormulaGetView(diskFact).actors, diskIndex).atom;
	FormulaView clause = DictionaryAddClauseFromCString("circle c radius r | ! disk c size r");

	Atom query = CStringToTerm("circle c radius 5.0");
	size32 nDisks;
	ASSERT_UINT32_EQUAL(runQueryCountAtom(query, circleRule.circleIndex, disk, &nDisks), 2)
	ASSERT_UINT32_EQUAL(nDisks, 1)
	// A second query yields the same tuples, and caches no new ifact
	ASSERT_UINT32_EQUAL(runQueryCountAtom(query, circleRule.circleIndex, disk, &nDisks), 2)
	ASSERT_UINT32_EQUAL(nDisks, 1)
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(RelationGetTupleStore(circleRule.relation)), 1)
	ReleaseFormula(query);

	DictionaryRemoveClause(&clause);
	DictionaryRemoveIFactRule(&(circleRule.ifactRule));
	RetractFact(FormulaGetView(diskFact));
	DropRelation(RelationFromFact(FormulaGetView(diskFact)));
	ReleaseFormula(diskFact);
	teardownCircleRule(&circleRule);
}


/**
 * CLAUDE: A TupleStore with the ID column last in its index order has a primitive service
 * (circle >ID radius <FLOAT). The query (circle c radius 5.0) then compiles to a UNION of
 * the IFACT operator and that service, reading the same TupleStore that the IFACT operator
 * writes; see unionSetupContext().
 */
void testCompileIFactRuleWithSeed(void)
{
	CircleRule circleRule;
	setupCircleRule(&circleRule);
	index8 indexColumns[2] = {circleRule.radiusIndex, circleRule.circleIndex};
	TupleStore * store = CreateTupleStore(circleRule.relation, GetStorageProvider(PROVIDER_BTREE), 2, indexColumns);
	Atom circle = CreateStringFromCString("c");
	Atom tuple[2];
	tuple[circleRule.circleIndex] = circle;
	tuple[circleRule.radiusIndex] = (Atom) {._float = 5.0};
	ASSERT_UINT32_EQUAL(TupleStoreAddTuple(store, tuple, 0), TUPLE_ADDED)

	Atom query = CStringToTerm("circle c radius 5.0");
	size32 nStored;
	ASSERT_UINT32_EQUAL(runQueryCountAtom(query, circleRule.circleIndex, circle, &nStored), 2)
	ASSERT_UINT32_EQUAL(nStored, 1)
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(store), 2)
	// A second query yields the same tuples, and caches no new ifact
	ASSERT_UINT32_EQUAL(runQueryCountAtom(query, circleRule.circleIndex, circle, &nStored), 2)
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(store), 2)

	// CLAUDE: The compiled service is a UNION of the IFACT operator and the primitive
	// service it replaced
	Service ifactService = createCircleRuleService(&circleRule, PARAMETER_OUT, PARAMETER_IN);
	assertIFactUnion(ifactService);

	// CLAUDE: Creating an INT relation of the term form invalidates the compiled service,
	// restoring the primitive service, marked stale; see ReplaceService()
	Relation intRelation = circleRule.relation;
	intRelation.typeSignature.atomTypes[circleRule.radiusIndex] = AT_INT;
	CreateTupleStore(intRelation, GetStorageProvider(PROVIDER_BTREE), 2, 0);
	ASSERT_UINT32_EQUAL(ServiceGetOperator(ifactService)->type, OPERATOR_MACHINE)
	ASSERT_TRUE(ServiceIsStale(ifactService))

	// CLAUDE: A new query compiles the same service again, reading the primitive service
	ASSERT_UINT32_EQUAL(runQueryCountAtom(query, circleRule.circleIndex, circle, &nStored), 2)
	ASSERT_UINT32_EQUAL(nStored, 1)
	assertIFactUnion(ifactService);
	ReleaseFormula(query);

	// Removing the rule removes the cached tuple, and leaves the stored one
	DictionaryRemoveIFactRule(&(circleRule.ifactRule));
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(store), 1)
	// CLAUDE: and restores the primitive service
	ASSERT_UINT32_EQUAL(ServiceGetOperator(ifactService)->type, OPERATOR_MACHINE)
	DropRelation(intRelation);
	ASSERT_UINT32_EQUAL(TupleStoreRemoveTuple(store, tuple, 0), TUPLE_REMOVED)
	IFactRelease(circle);
	teardownCircleRule(&circleRule);
}


/**
 * CLAUDE: The queries (circle c radius 3.14) and (circle c radius 5) create one relation
 * each from the ifact rule (circle * radius r). Creating the INT relation invalidates the
 * IFACT service of the FLOAT relation, but the cached ifact of the FLOAT relation is kept.
 */
void testCompileIFactRuleTwoTypes(void)
{
	CircleRule circleRule;
	setupCircleRule(&circleRule);
	TypedTuple * tuple;

	Atom floatQuery = CStringToTerm("circle c radius 3.14");
	ASSERT_UINT32_EQUAL(runQuery(floatQuery, &tuple), 1)
	FreeTypedTuple(tuple);
	ReleaseFormula(floatQuery);
	TupleStore * floatStore = RelationGetTupleStore(circleRule.relation);
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(floatStore), 1)

	Atom intQuery = CStringToTerm("circle c radius 5");
	ASSERT_UINT32_EQUAL(runQuery(intQuery, &tuple), 1)
	FreeTypedTuple(tuple);
	ReleaseFormula(intQuery);
	Relation intRelation = circleRule.relation;
	intRelation.typeSignature.atomTypes[circleRule.radiusIndex] = AT_INT;
	TupleStore * intStore = RelationGetTupleStore(intRelation);
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(intStore), 1)
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(floatStore), 1)

	// Both cached circles are found
	Atom allQuery = CStringToTerm("circle c radius r");
	ASSERT_UINT32_EQUAL(runQuery(allQuery, &tuple), 2)
	FreeTypedTuple(tuple);
	ReleaseFormula(allQuery);

	// Removing the rule releases the cached ifacts of both relations
	DictionaryRemoveIFactRule(&(circleRule.ifactRule));
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(floatStore), 0)
	ASSERT_UINT32_EQUAL(TupleStoreNTuples(intStore), 0)
	DropRelation(intRelation);
	teardownCircleRule(&circleRule);
}


int main(int argc, char * argv[])
{
	KernelInitialize(TRANSIENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testIFactOperator);
	ExecuteTest(testIFactQuery);
	ExecuteTest(testIFactRuleInvalidation);
	ExecuteTest(testCompileIFactRule);
	ExecuteTest(testCompileIFactRuleWithClause);
	ExecuteTest(testCompileIFactRuleWithSeed);
	ExecuteTest(testCompileIFactRuleTwoTypes);

	UnloadLibraries();
	KernelShutdown();
	TestSummary();
}
