
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/MixedTypeRelation.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "kernel/ServiceRegistry.h"
#include "library/string.h"
#include "lang/formula.h"
#include "storage/RelationBTree.h"
#include "library/MachineService.h"
#include "parser/TermBuilder.h"
#include "testing/fixtures.h"
#include "library/list.h"
#include "testing/testing.h"


static RelationFixture edgeFixture;


/**
 * Return the number of tuples in the relation returned for the given query
 */
static size32 countQueryTuples(char const * queryString)
{
	Atom query = CStringToTerm(queryString);
	MixedTypeRelation * relation = CreateConcatRelation(FormulaGetForm(query), FormulaGetActors(query));
	size32 nTuples = 0;
	while(MixedTypeRelationNext(relation))
		nTuples++;
	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);
	return nTuples;
}


/**
 * Return the number of services the relation for the given query read from, having read
 * it to its end.
 */
static size32 countQueryServices(char const * queryString)
{
	Atom query = CStringToTerm(queryString);
	MixedTypeRelation * relation = CreateConcatRelation(FormulaGetForm(query), FormulaGetActors(query));
	while(MixedTypeRelationNext(relation))
		;
	size32 nServices = MixedTypeRelationNServices(relation);
	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);
	return nServices;
}


/**
 * A query with a variable at every position must enumerate the whole relation,
 * and the tuples arrive in the actor order of the query.
 */
void testConcatEveryTuple(void)
{
	SetupEdgeFixture(&edgeFixture);
	Atom query = CStringToTerm("edge e from x to y");

	MixedTypeRelation * relation = CreateConcatRelation(FormulaGetForm(query), FormulaGetActors(query));
	ASSERT_DATA64_EQUAL(relation->termForm.hash, FormulaGetForm(query).hash)

	// Each tuple of the relation arrives once, though we do not know in which order,
	// as the relation is stored sorted by atom
	bool foundTuple[EDGE_N_EDGES] = {false, false, false, false};
	size32 nTuples = 0;
	while(MixedTypeRelationNext(relation)) {
		TypedTuple const * tuple = MixedTypeRelationPeekTuple(relation);
		ASSERT_UINT32_EQUAL(tuple->nAtoms, 3)
		for(index8 i = 0; i < 3; i++)
			ASSERT_UINT32_EQUAL(TypedTupleGetElement(tuple, i).type, AT_ID)
		for(index8 i = 0; i < EDGE_N_EDGES; i++) {
			if(TypedTupleEqual(tuple, edgeFixture.tuples[i])) {
				ASSERT_FALSE(foundTuple[i])
				foundTuple[i] = true;
			}
		}
		nTuples++;
	}
	ASSERT_UINT32_EQUAL(nTuples, EDGE_N_EDGES)
	for(index8 i = 0; i < EDGE_N_EDGES; i++)
		ASSERT_TRUE(foundTuple[i])

	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);
	TeardownRelationFixture(&edgeFixture);
}


/**
 * A variable occurring twice in a query constrains the two positions to be equal.
 * Here, the query (edge e from x to x) asks for the self-edges. Since the service
 * this dispatches to enumerates every edge, MixedTypeRelation must filter the tuples.
 */
void testConcatRepeatedVariable(void)
{
	SetupEdgeFixture(&edgeFixture);
	Atom query = CStringToTerm("edge e from x to x");

	MixedTypeRelation * relation = CreateConcatRelation(FormulaGetForm(query), FormulaGetActors(query));
	size32 nTuples = 0;
	while(MixedTypeRelationNext(relation)) {
		TypedTuple const * tuple = MixedTypeRelationPeekTuple(relation);
		ASSERT_DATA64_EQUAL(
			TypedTupleGetAtom(tuple, RelationFixtureRoleIndex(&edgeFixture, "from")).hash,
			TypedTupleGetAtom(tuple, RelationFixtureRoleIndex(&edgeFixture, "to")).hash)
		nTuples++;
	}
	ASSERT_UINT32_EQUAL(nTuples, 2)
	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);

	// Each occurence of the anonymous variable is a variable of its own, and so
	// constrains nothing
	ASSERT_UINT32_EQUAL(countQueryTuples("edge _ from _ to _"), EDGE_N_EDGES)

	TeardownRelationFixture(&edgeFixture);
}


/**
 * Test MixedTypeRelation on queries with all inputs, yielding 1 or 0 tuples.
 */
void testConcatConstantQuery(void)
{
	SetupEdgeFixture(&edgeFixture);

	// The edge eq is the self edge of a
	ASSERT_UINT32_EQUAL(countQueryTuples("edge \"eq\" from \"a\" to \"a\""), 1)
	ASSERT_UINT32_EQUAL(countQueryTuples("edge \"eq\" from \"a\" to \"b\""), 0)

	TeardownRelationFixture(&edgeFixture);
}


/**
 * Test MixedTypeRelation with two underlying services having distinct atom types.
 * This requires concatenating tuples from the two relations.
 */
void testConcatAcrossRelations(void)
{
	Atom termForm = CreateTermFormFromRoleNames(
		(char const * []) {"first", "second"}, 2, true);

	// Two relation tables for the term form, one per combination of column types
	Relation const * idRelation = CreateRelation(
		termForm, 2, CreateTypeSignature((byte[]) {AT_ID, AT_ID}, 2));
	RelationTable * idTable = CreateRelationTable(
		idRelation, &btreeTableProvider, (index8[]) {0, 1});
	ReleaseRelation(idRelation);
	Relation const * intRelation = CreateRelation(
		termForm, 2, CreateTypeSignature((byte[]) {AT_ID, AT_INT}, 2));
	RelationTable * intTable = CreateRelationTable(
		intRelation, &btreeTableProvider, (index8[]) {0, 1});
	ReleaseRelation(intRelation);

	TypedAtom idActors[2] = {
		CreateTypedAtom(AT_ID, CreateStringFromCString("a")),
		CreateTypedAtom(AT_ID, CreateStringFromCString("b"))
	};
	TypedAtom intActors[2] = {
		CreateTypedAtom(AT_ID, CreateStringFromCString("c")),
		CreateTypedAtom(AT_INT, (Atom) {._int = 42})
	};
	TypedTuple * idTuple = CreateTypedTupleFromArray(idActors, 2);
	TypedTuple * intTuple = CreateTypedTupleFromArray(intActors, 2);
	RelationTableAddTuple(idTable, TypedTuplePeekAtoms(idTuple), 0);
	RelationTableAddTuple(intTable, TypedTuplePeekAtoms(intTuple), 0);
	for(index8 i = 0; i < 2; i++) {
		ReleaseTypedAtom(idActors[i]);
		ReleaseTypedAtom(intActors[i]);
	}

	Atom query = CStringToTerm("first x second y");
	MixedTypeRelation * relation = CreateConcatRelation(FormulaGetForm(query), FormulaGetActors(query));

	size32 nTuples = 0;
	bool foundIdTuple = false;
	bool foundIntTuple = false;
	while(MixedTypeRelationNext(relation)) {
		TypedTuple const * tuple = MixedTypeRelationPeekTuple(relation);
		foundIdTuple = foundIdTuple || TypedTupleEqual(tuple, idTuple);
		foundIntTuple = foundIntTuple || TypedTupleEqual(tuple, intTuple);
		nTuples++;
	}
	// One tuple of each relation, with the column types of the relation it came from
	ASSERT_UINT32_EQUAL(nTuples, 2)
	ASSERT_TRUE(foundIdTuple)
	ASSERT_TRUE(foundIntTuple)

	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);

	RelationTableRemoveTuple(idTable, TypedTuplePeekAtoms(idTuple), 0);
	RelationTableRemoveTuple(intTable, TypedTuplePeekAtoms(intTuple), 0);
	FreeTypedTuple(idTuple);
	FreeTypedTuple(intTuple);
	DropRelationTable(intTable);
	DropRelationTable(idTable);
	IFactRelease(termForm);
}


/**
 * A variable repeated across role where the underylying service has different atom types
 * should return no tuples. Here, for the service (list <ID position >INT element >LETTER)
 * the constraint (list "ab" position x element x should yield no tuples.
 */
void testConcatRepeatedVariableAcrossTypes(void)
{
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" position p element e"), 2)
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" position x element x"), 0)
}


/**
 * A query no service answers and a query a service answers with no tuples both yield
 * nothing, and the count of services read is what tells the two apart.
 */
void testConcatServiceCount(void)
{
	// two services answer the (list position element) form, one per element type
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" position p element e"), 2)
	ASSERT_UINT32_EQUAL(countQueryServices("list \"ab\" position p element e"), 2)

	// those same services are read for this query, whose repeated variable drops
	// every tuple they yield
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" position x element x"), 0)
	ASSERT_UINT32_EQUAL(countQueryServices("list \"ab\" position x element x"), 2)

	// no service answers this form at all
	ASSERT_UINT32_EQUAL(countQueryTuples("nowhere x nothing y"), 0)
	ASSERT_UINT32_EQUAL(countQueryServices("nowhere x nothing y"), 0)
}


/**
 * Constructing a MixedTypeRelation with a query that does not match any service
 * yields no tuples.
 */
void testConcatWithoutMatch(void)
{
	Atom unknownQuery = CStringToTerm("nowhere x nothing y");
	MixedTypeRelation * relation = CreateConcatRelation(
		FormulaGetForm(unknownQuery), FormulaGetActors(unknownQuery));
	ASSERT_FALSE(MixedTypeRelationNext(relation))
	ASSERT_UINT32_EQUAL(MixedTypeRelationNServices(relation), 0)
	// A relation read past its last tuple stays empty, and neither the dispatch
	// iterator nor a service is called again
	ASSERT_FALSE(MixedTypeRelationNext(relation))
	FreeMixedTypeRelation(relation);
	ReleaseFormula(unknownQuery);
}


/**
 * A relation may be freed at any position, so a caller that has seen the tuples it
 * wanted need not read the relation to its end. The service it was reading and the
 * dispatch iterator are released either way.
 */
void testConcatAbandonedIteration(void)
{
	SetupEdgeFixture(&edgeFixture);

	Atom query = CStringToTerm("edge e from x to y");
	MixedTypeRelation * relation = CreateConcatRelation(FormulaGetForm(query), FormulaGetActors(query));
	ASSERT_TRUE(MixedTypeRelationNext(relation))
	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);

	TeardownRelationFixture(&edgeFixture);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	ListSetup();
	StringSetup();

	ExecuteTest(testConcatEveryTuple);
	ExecuteTest(testConcatRepeatedVariable);
	ExecuteTest(testConcatConstantQuery);
	ExecuteTest(testConcatAcrossRelations);
	ExecuteTest(testConcatRepeatedVariableAcrossTypes);
	ExecuteTest(testConcatServiceCount);
	ExecuteTest(testConcatWithoutMatch);
	ExecuteTest(testConcatAbandonedIteration);

	FreeMachineServices();
	StringShutdown();
	ListShutdown();
	KernelShutdown();
	TestSummary();
}
