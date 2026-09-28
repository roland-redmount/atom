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
	fixture->store = CreateTupleStore(fixture->relation, &btreeStorageProvider, 2, indexColumns);

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

	// Removing the IFACT service removes the cached ifacts
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


int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testIFactOperator);
	ExecuteTest(testIFactQuery);

	UnloadLibraries();
	KernelShutdown();
	TestSummary();
}
