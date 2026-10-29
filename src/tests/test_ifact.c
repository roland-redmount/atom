#include "kernel/dictionary.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/MixedTypeRelation.h"
#include "kernel/operator.h"
#include "kernel/TypedRelation.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/TupleStore.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/library.h"
#include "library/string.h"
#include "lang/Variable.h"
#include "parser/ConjunctionBuilder.h"
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
	TypedRelation relation;
	index8 circleIndex;
	index8 radiusIndex;
	Service ifactService;
	// The service reading the store with the ID column as the only input
	// CLAUDE: The relation has no TupleStore; the service reads the ifacts of the relation,
	// see IFactCreateReaderService()
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
	fixture->relation = (TypedRelation) {
		.form = fixture->termForm,
		.typeSignature = CreateTypeSignature(atomTypes, 2)
	};
	IFactCreateReaderService(fixture->relation, fixture->circleIndex);

	fixture->ifactService = createCircleService(fixture, PARAMETER_OUT, PARAMETER_IN);
	fixture->byIdService = createCircleService(fixture, PARAMETER_IN, PARAMETER_OUT);
	IFactTerm ifactTerm = {
		.relation = fixture->relation,
		.idColumn = fixture->circleIndex,
		.argumentMap = {0, 1}
	};
	CreateService(fixture->ifactService, CreateIFactOperator(&ifactTerm, 1, 2));
}


/**
 * Remove the service reading the store by ID, which also removes the IFACT service
 * depending on it, then drop the relation.
 */
/* CLAUDE: The IFACT service does not depend on the service reading the store, so the
   IFACT service is removed by DropRelation(), if it still exists. */
static void teardownCircleFixture(CircleFixture * fixture)
{
	// CLAUDE: The IFACT operator of the fixture is not compiled from an ifact rule,
	// so its cached ifacts are released here
	IFactReleaseCached(fixture->termForm, &(fixture->circleIndex));
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(fixture->relation), 0)
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
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(fixture.relation), 1)
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nFixtureIFacts + 1)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(circle5), 1)

	// A second call yields the existing ifact, and the cache still holds one reference
	ASSERT_TRUE(SameAtoms(callCircleIFact(&fixture, 5.0), circle5))
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(fixture.relation), 1)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(circle5), 1)

	// Another radius yields another ifact
	Atom circle6 = callCircleIFact(&fixture, 6.0);
	ASSERT_FALSE(SameAtoms(circle6, circle5))
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(fixture.relation), 2)
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nFixtureIFacts + 2)

	// A cached tuple cannot be retracted
	// CLAUDE: The relation has no TupleStore; see IFactCreateReaderService()
	TypedAtom actors[2];
	actors[fixture.circleIndex] = CreateTypedAtom(AT_ID, circle5);
	actors[fixture.radiusIndex] = CreateTypedAtom(AT_FLOAT, (Atom) {._float = 5.0});
	Atom fact = CreateFormulaFromArray(fixture.termForm, actors);
	ASSERT_INT32_EQUAL(RetractFact(FormulaGetView(fact)), RETRACT_NOT_ASSERTED)
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(fixture.relation), 2)
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
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(fixture.relation), 1)

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
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(fixture.relation), 1)

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
	// CLAUDE: The fixture relation has no TupleStore; see setupCircleFixture()
	CircleFixture fixture;
	setupCircleFixture(&fixture);
	// Evaluate the IFACT operator, caching a tuple
	Atom circle = callCircleIFact(&fixture, 5.0);
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(fixture.relation), 1)

	// Add the ifact rule (circle * radius r), invalidating existing services
	Atom rule = CStringToTerm("circle * radius r");
	FormulaView ifactRule = DictionaryAddIFactRule(rule);
	ReleaseFormula(rule);

	// The existing IFACT service is removed
	ASSERT_TRUE(ServiceGetOperator(fixture.ifactService) == 0)
	// the cached tuple is kept; normally released by DictionaryRemoveIFactRule()
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(fixture.relation), 1)
	// CLAUDE: The primitive service reading the ifacts is marked stale
	ASSERT_TRUE(ServiceIsStale(fixture.byIdService))

	// CLAUDE: A query reading the cached tuple by its ID still yields the tuple,
	// by marking the primitive service non-stale
	Atom variablesQuery = CStringToTerm("circle x radius r");
	FormulaView variablesView = FormulaGetView(variablesQuery);
	TypedAtom actors[2];
	for(index8 i = 0; i < 2; i++)
		actors[i] = TypedTupleGetElement(variablesView.actors, i);
	actors[fixture.circleIndex] = CreateTypedAtom(AT_ID, circle);
	Atom byIdQuery = CreateFormulaFromArray(FormulaGetForm(variablesQuery), actors);
	ReleaseFormula(variablesQuery);
	TypedTuple * tuple;
	ASSERT_UINT32_EQUAL(runQuery(byIdQuery, &tuple), 1)
	ASSERT_TRUE(TypedTupleGetElement(tuple, fixture.radiusIndex).atom._float == 5.0)
	FreeTypedTuple(tuple);
	ReleaseFormula(byIdQuery);
	ASSERT_FALSE(ServiceIsStale(fixture.byIdService))

	// A relation of the term form created after the rule has stale primitive services
	TypedRelation intRelation = {
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
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(fixture.relation), 0)
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
	TypedRelation relation;
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
	circleRule->relation = (TypedRelation) {
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
	ASSERT_TRUE(RelationGetTupleStore(circleRule.relation) == 0)
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(circleRule.relation), 1)
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
	// CLAUDE: The cached ifact is not a fact, and the relation has no service with the circle
	// as output, so the query yields nothing
	ASSERT_UINT32_EQUAL(runQuery(variablesQuery, &tuple), 0)
	ReleaseFormula(variablesQuery);
	Service allOutputService = createCircleRuleService(&circleRule, PARAMETER_OUT, PARAMETER_OUT);
	ASSERT_TRUE(ServiceGetOperator(allOutputService) == 0)
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(circleRule.relation), 1)

	// Removing the rule removes the IFACT service and the cached ifact
	// A primitive service of the same signature, replaced by the IFACT service,
	// is restored; see ReplaceService()
	DictionaryRemoveIFactRule(&(circleRule.ifactRule));
	Operator const * ifactServiceOperator = ServiceGetOperator(ifactService);
	ASSERT_TRUE(!ifactServiceOperator || (ifactServiceOperator->type == OPERATOR_MACHINE))
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(circleRule.relation), 0)
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nIFacts)
	teardownCircleRule(&circleRule);
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
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(circleRule.relation), 1)
	ReleaseFormula(query);

	DictionaryRemoveClause(&clause);
	DictionaryRemoveIFactRule(&(circleRule.ifactRule));
	RetractFact(FormulaGetView(diskFact));
	DropRelation(RelationFromFact(FormulaGetView(diskFact)));
	ReleaseFormula(diskFact);
	teardownCircleRule(&circleRule);
}


/**
 * CLAUDE: The query (circle c radius r) compiles a service from the clause
 * (circle c radius r | ! disk c size r), and this service alone holds the relation
 * (circle ID radius FLOAT). The relation has no TupleStore, so the query
 * (circle c radius 5.0) then adds a service reading the ifacts of the relation,
 * and compiles to a UNION of the IFACT operator and the clause; see IFactSetupRelation().
 */
void testIFactRuleAfterCompiledService(void)
{
	CircleRule circleRule;
	setupCircleRule(&circleRule);
	Atom diskFact = CStringToTerm("disk \"d\" size 5.0");
	ASSERT_INT32_EQUAL(AssertFormula(diskFact), ASSERT_OK)
	index8 diskIndex = findRoleIndex(FormulaGetForm(diskFact), "disk");
	Atom disk = TypedTupleGetElement(FormulaGetView(diskFact).actors, diskIndex).atom;
	FormulaView clause = DictionaryAddClauseFromCString("circle c radius r | ! disk c size r");

	TypedTuple * tuple;
	Atom allQuery = CStringToTerm("circle c radius r");
	ASSERT_UINT32_EQUAL(runQuery(allQuery, &tuple), 1)
	FreeTypedTuple(tuple);
	ReleaseFormula(allQuery);
	ASSERT_TRUE(RelationExists(circleRule.relation))
	ASSERT_TRUE(RelationGetTupleStore(circleRule.relation) == 0)
	ASSERT_FALSE(IFactRelationHasReader(circleRule.relation))

	Atom query = CStringToTerm("circle c radius 5.0");
	size32 nDisks;
	ASSERT_UINT32_EQUAL(runQueryCountAtom(query, circleRule.circleIndex, disk, &nDisks), 2)
	ASSERT_UINT32_EQUAL(nDisks, 1)
	ReleaseFormula(query);
	ASSERT_TRUE(IFactRelationHasReader(circleRule.relation))
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(circleRule.relation), 1)

	DictionaryRemoveClause(&clause);
	DictionaryRemoveIFactRule(&(circleRule.ifactRule));
	RetractFact(FormulaGetView(diskFact));
	DropRelation(RelationFromFact(FormulaGetView(diskFact)));
	ReleaseFormula(diskFact);
	teardownCircleRule(&circleRule);
}


/**
 * CLAUDE: An ifact rule does not apply to a relation with a TupleStore, which holds
 * asserted facts; see IFactSetupRelation(). With a B-tree TupleStore for the relation
 * (circle ID radius FLOAT), the query (circle c radius 5.0) reads the B-tree only,
 * and creates no ifact.
 */
void testIFactRuleWithStore(void)
{
	CircleRule circleRule;
	setupCircleRule(&circleRule);
	index8 indexColumns[2] = {circleRule.radiusIndex, circleRule.circleIndex};
	TupleStore * store = CreateTupleStore(circleRule.relation, GetStorageProvider(PROVIDER_BTREE), 2, indexColumns);
	Atom circle = CreateStringFromCString("c");
	Atom tuple[2];
	tuple[circleRule.circleIndex] = circle;
	tuple[circleRule.radiusIndex] = (Atom) {._float = 5.0};
	ASSERT_UINT32_EQUAL(TupleStoreAddTuple(store, tuple), TUPLE_ADDED)
	size32 nIFacts = IFactTotalCount();

	Atom query = CStringToTerm("circle c radius 5.0");
	size32 nStored;
	ASSERT_UINT32_EQUAL(runQueryCountAtom(query, circleRule.circleIndex, circle, &nStored), 1)
	ASSERT_UINT32_EQUAL(nStored, 1)
	ReleaseFormula(query);
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nIFacts)
	Service ifactService = createCircleRuleService(&circleRule, PARAMETER_OUT, PARAMETER_IN);
	ASSERT_UINT32_EQUAL(ServiceGetOperator(ifactService)->type, OPERATOR_MACHINE)

	DictionaryRemoveIFactRule(&(circleRule.ifactRule));
	ASSERT_UINT32_EQUAL(TupleStoreRemoveTuple(store, tuple), TUPLE_REMOVED)
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
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(circleRule.relation), 1)

	Atom intQuery = CStringToTerm("circle c radius 5");
	ASSERT_UINT32_EQUAL(runQuery(intQuery, &tuple), 1)
	FreeTypedTuple(tuple);
	ReleaseFormula(intQuery);
	TypedRelation intRelation = circleRule.relation;
	intRelation.typeSignature.atomTypes[circleRule.radiusIndex] = AT_INT;
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(intRelation), 1)
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(circleRule.relation), 1)

	// Both cached circles are found
	// CLAUDE: A cached ifact is not a fact, so neither circle is found
	Atom allQuery = CStringToTerm("circle c radius r");
	ASSERT_UINT32_EQUAL(runQuery(allQuery, &tuple), 0)
	ReleaseFormula(allQuery);

	// Removing the rule releases the cached ifacts of both relations
	DictionaryRemoveIFactRule(&(circleRule.ifactRule));
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(circleRule.relation), 0)
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(intRelation), 0)
	DropRelation(intRelation);
	teardownCircleRule(&circleRule);
}


/*
 * CLAUDE: Create a copy of the formula with every occurrence of the named variable
 * replaced by the given atom. The caller must release the returned formula.
 */
static Atom substituteVariable(Atom formula, char variableName, TypedAtom atom)
{
	FormulaView view = FormulaGetView(formula);
	size8 arity = view.actors->nAtoms;
	TypedAtom actors[arity];
	Atom variable = CreateVariable(variableName);
	for(index8 i = 0; i < arity; i++) {
		actors[i] = TypedTupleGetElement(view.actors, i);
		if((actors[i].type == AT_VARIABLE) && SameVariable(actors[i].atom, variable))
			actors[i] = atom;
	}
	return CreateFormulaFromArray(view.form, actors);
}


/*
 * CLAUDE: Run a query and return the ID atom of its single result tuple that is not a string.
 */
static TypedAtom runQueryForID(char const * conjunction)
{
	Atom query = CStringToConjunction(conjunction);
	TypedTuple * tuple;
	ASSERT_UINT32_EQUAL(runQuery(query, &tuple), 1)
	ReleaseFormula(query);
	TypedAtom id = {0};
	for(index8 i = 0; i < tuple->nAtoms; i++) {
		TypedAtom element = TypedTupleGetElement(tuple, i);
		if((element.type == AT_ID) && !IsString(element.atom))
			id = element;
	}
	FreeTypedTuple(tuple);
	ASSERT_UINT32_EQUAL(id.type, AT_ID)
	return id;
}


/**
 * CLAUDE: The conjunction query (car x brand "Volvo" & car x model "Amazon") compiles to an
 * IFACT operator from the ifact rule (car * brand b & car * model m). The ifact has one tuple
 * in each of the relations (car ID brand STRING) and (car ID model STRING), and each term is
 * read by its ID.
 */
void testCompileIFactConjunctionRule(void)
{
	Atom rule = CStringToConjunction("car * brand b & car * model m");
	FormulaView ifactRule = DictionaryAddIFactRule(rule);
	Atom volvo = CreateStringFromCString("Volvo");
	Atom amazon = CreateStringFromCString("Amazon");
	size32 nIFacts = IFactTotalCount();

	TypedAtom car = runQueryForID("car x brand \"Volvo\" & car x model \"Amazon\"");
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nIFacts + 1)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(car.atom), 1)

	// A second query, also with the terms in another order, yields the same car
	ASSERT_TRUE(SameTypedAtoms(runQueryForID("car x brand \"Volvo\" & car x model \"Amazon\""), car))
	ASSERT_TRUE(SameTypedAtoms(runQueryForID("car x model \"Amazon\" & car x brand \"Volvo\""), car))
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nIFacts + 1)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(car.atom), 1)

	// Another model yields another car
	TypedAtom otherCar = runQueryForID("car x brand \"Volvo\" & car x model \"Duett\"");
	ASSERT_FALSE(SameTypedAtoms(otherCar, car))

	// Each term of the ifact is read by the car ID
	TypedTuple * tuple;
	Atom brandQuery = CStringToTerm("car c brand b");
	Atom brandByID = substituteVariable(brandQuery, 'c', car);
	ASSERT_UINT32_EQUAL(runQuery(brandByID, &tuple), 1)
	ASSERT_TRUE(TypedTupleContainsAtom(tuple, CreateTypedAtom(AT_ID, volvo)))
	FreeTypedTuple(tuple);
	Atom modelQuery = CStringToTerm("car c model m");
	Atom modelByID = substituteVariable(modelQuery, 'c', car);
	ASSERT_UINT32_EQUAL(runQuery(modelByID, &tuple), 1)
	ASSERT_TRUE(TypedTupleContainsAtom(tuple, CreateTypedAtom(AT_ID, amazon)))
	FreeTypedTuple(tuple);

	// CLAUDE: A string is an ID atom, so both columns of each relation have type AT_ID
	byte atomTypes[2] = {AT_ID, AT_ID};
	TypedRelation brandRelation = {
		.form = FormulaGetForm(brandQuery),
		.typeSignature = CreateTypeSignature(atomTypes, 2)
	};
	TypedRelation modelRelation = {
		.form = FormulaGetForm(modelQuery),
		.typeSignature = CreateTypeSignature(atomTypes, 2)
	};
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(brandRelation), 2)
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(modelRelation), 2)
	ReleaseFormula(brandByID);
	ReleaseFormula(modelByID);
	ReleaseFormula(brandQuery);
	ReleaseFormula(modelQuery);

	// Removing the rule releases both cached ifacts
	DictionaryRemoveIFactRule(&ifactRule);
	ASSERT_FALSE(IFactExists(car.atom))
	ASSERT_FALSE(IFactExists(otherCar.atom))
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(brandRelation), 0)
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nIFacts)
	DropRelation(brandRelation);
	DropRelation(modelRelation);
	ReleaseFormula(rule);
	IFactRelease(volvo);
	IFactRelease(amazon);
}


/*
 * CLAUDE: Run a term query and return the atom in the "pair" role of its single result.
 */
static TypedAtom runQueryForPair(Atom query)
{
	TypedTuple * tuple;
	ASSERT_UINT32_EQUAL(runQuery(query, &tuple), 1)
	TypedAtom pair = TypedTupleGetElement(tuple, findRoleIndex(FormulaGetForm(query), "pair"));
	FreeTypedTuple(tuple);
	ASSERT_UINT32_EQUAL(pair.type, AT_ID)
	return pair;
}


/*
 * CLAUDE: Run the query written by the C string, with the variable p replaced by the given pair,
 * and return the number of results.
 */
static size32 countPairResults(char const * queryString, TypedAtom pair)
{
	Atom query = CStringToTerm(queryString);
	Atom byIdQuery = substituteVariable(query, 'p', pair);
	TypedTuple * tuple;
	size32 nResults = runQuery(byIdQuery, &tuple);
	if(tuple)
		FreeTypedTuple(tuple);
	ReleaseFormula(byIdQuery);
	ReleaseFormula(query);
	return nResults;
}


/**
 * CLAUDE: With the ifact rule (pair * member a member b), whose role "member" holds two actors,
 * the actors of the role may be given in either order. The ifact stores the tuple in canonical
 * order, so (pair x member 3 member 5) and (pair x member 5 member 3) yield the same pair, also
 * through the clause (link p from a to b | ! pair p member a member b). The reader of the
 * relation yields the tuple in both orders, so a query with the pair bound finds the other
 * member from either member; see IFactReaderState.
 */
void testIFactRepeatedRole(void)
{
	Atom rule = CStringToTerm("pair * member a member b");
	FormulaView ifactRule = DictionaryAddIFactRule(rule);
	FormulaView clause = DictionaryAddClauseFromCString("link p from a to b | ! pair p member a member b");

	Atom query35 = CStringToTerm("pair x member 3 member 5");
	Atom query53 = CStringToTerm("pair x member 5 member 3");
	TypedAtom pair = runQueryForPair(query35);
	ASSERT_TRUE(SameTypedAtoms(runQueryForPair(query53), pair))
	ASSERT_UINT32_EQUAL(IFactReferenceCount(pair.atom), 1)

	// CLAUDE: CreateIFact() stores the same canonical tuple
	Atom ifactFormula = CStringToTerm("pair * member 5 member 3");
	Atom createdPair = CreateIFact(FormulaGetView(ifactFormula));
	ASSERT_DATA64_EQUAL(createdPair.hash, pair.atom.hash)
	IFactRelease(createdPair);
	ReleaseFormula(ifactFormula);

	// CLAUDE: The pair read with either member bound, and with no member bound
	ASSERT_UINT32_EQUAL(countPairResults("pair p member 5 member 3", pair), 1)
	ASSERT_UINT32_EQUAL(countPairResults("pair p member 3 member 5", pair), 1)
	ASSERT_UINT32_EQUAL(countPairResults("pair p member 5 member b", pair), 1)
	ASSERT_UINT32_EQUAL(countPairResults("pair p member 3 member b", pair), 1)
	ASSERT_UINT32_EQUAL(countPairResults("pair p member 4 member b", pair), 0)
	ASSERT_UINT32_EQUAL(countPairResults("pair p member a member b", pair), 1)

	// CLAUDE: Equal members give one fact, with no duplicate tuple
	Atom query44 = CStringToTerm("pair x member 4 member 4");
	TypedAtom pair44 = runQueryForPair(query44);
	ASSERT_UINT32_EQUAL(countPairResults("pair p member a member b", pair44), 1)

	// CLAUDE: The clause binds the members in either order
	Atom link53 = CStringToTerm("link p from 5 to 3");
	Atom link35 = CStringToTerm("link p from 3 to 5");
	TypedTuple * tuple;
	ASSERT_UINT32_EQUAL(runQuery(link53, &tuple), 1)
	ASSERT_TRUE(SameTypedAtoms(TypedTupleGetElement(tuple, findRoleIndex(FormulaGetForm(link53), "link")), pair))
	FreeTypedTuple(tuple);
	ASSERT_UINT32_EQUAL(runQuery(link35, &tuple), 1)
	ASSERT_TRUE(SameTypedAtoms(TypedTupleGetElement(tuple, findRoleIndex(FormulaGetForm(link35), "link")), pair))
	FreeTypedTuple(tuple);

	byte atomTypes[3] = {AT_INT, AT_INT, AT_INT};
	atomTypes[findRoleIndex(FormulaGetForm(rule), "pair")] = AT_ID;
	TypedRelation relation = {
		.form = FormulaGetForm(rule),
		.typeSignature = CreateTypeSignature(atomTypes, 3)
	};
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(relation), 2)

	ReleaseFormula(link35);
	ReleaseFormula(link53);
	ReleaseFormula(query44);
	ReleaseFormula(query53);
	ReleaseFormula(query35);
	DictionaryRemoveClause(&clause);
	DictionaryRemoveIFactRule(&ifactRule);
	ASSERT_UINT32_EQUAL(IFactRelationNTuples(relation), 0)
	DropRelation(relation);
	ReleaseFormula(rule);
}


/**
 * CLAUDE: The clause (make c brand b model m | ! car c brand b | ! car c model m) reads the
 * ifacts of the rule (car * brand b & car * model m). The term set of the clause body compiles
 * to the IFACT operator of the rule, with no conjunction query compiled before; see
 * compileNextIFactTerms(). The car is the same as for the conjunction query.
 */
void testCompileIFactConjunctionRuleInClause(void)
{
	Atom rule = CStringToConjunction("car * brand b & car * model m");
	FormulaView ifactRule = DictionaryAddIFactRule(rule);
	FormulaView clause = DictionaryAddClauseFromCString(
		"make c brand b model m | ! car c brand b | ! car c model m");
	size32 nIFacts = IFactTotalCount();

	Atom query = CStringToTerm("make c brand \"Volvo\" model \"Amazon\"");
	TypedTuple * tuple;
	ASSERT_UINT32_EQUAL(runQuery(query, &tuple), 1)
	index8 makeIndex = findRoleIndex(FormulaGetForm(query), "make");
	TypedAtom car = TypedTupleGetElement(tuple, makeIndex);
	FreeTypedTuple(tuple);
	ASSERT_UINT32_EQUAL(car.type, AT_ID)
	ASSERT_FALSE(IsString(car.atom))
	ASSERT_UINT32_EQUAL(IFactReferenceCount(car.atom), 1)
	ASSERT_TRUE(SameTypedAtoms(runQueryForID("car x brand \"Volvo\" & car x model \"Amazon\""), car))
	ASSERT_UINT32_EQUAL(IFactReferenceCount(car.atom), 1)

	// CLAUDE: The term forms of the relations are held by the rule
	Atom brandQuery = CStringToTerm("car c brand b");
	Atom modelQuery = CStringToTerm("car c model m");
	byte atomTypes[2] = {AT_ID, AT_ID};
	TypedRelation brandRelation = {
		.form = FormulaGetForm(brandQuery),
		.typeSignature = CreateTypeSignature(atomTypes, 2)
	};
	TypedRelation modelRelation = {
		.form = FormulaGetForm(modelQuery),
		.typeSignature = CreateTypeSignature(atomTypes, 2)
	};
	ReleaseFormula(query);
	DictionaryRemoveIFactRule(&ifactRule);
	ASSERT_FALSE(IFactExists(car.atom))
	ASSERT_UINT32_EQUAL(IFactTotalCount(), nIFacts)
	DictionaryRemoveClause(&clause);
	DropRelation(brandRelation);
	DropRelation(modelRelation);
	ReleaseFormula(brandQuery);
	ReleaseFormula(modelQuery);
	ReleaseFormula(rule);
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
	ExecuteTest(testIFactRuleWithStore);
	ExecuteTest(testIFactRuleAfterCompiledService);
	ExecuteTest(testCompileIFactRuleTwoTypes);
	ExecuteTest(testCompileIFactConjunctionRule);
	ExecuteTest(testCompileIFactConjunctionRuleInClause);
	ExecuteTest(testIFactRepeatedRole);

	UnloadLibraries();
	KernelShutdown();
	return TestSummary();
}
