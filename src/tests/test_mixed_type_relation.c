
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/MixedTypeRelation.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "lang/Formula.h"
#include "storage/RelationBTree.h"
#include "library/MachineService.h"
#include "parser/TermBuilder.h"
#include "testing/fixtures.h"
#include "testing/testing.h"


static RelationFixture edgeFixture;


/**
 * Return the number of tuples in the relation returned for the given query
 */
static size32 countQueryTuples(char const * queryString)
{
	Formula * query = CStringToTerm(queryString);
	MixedTypeRelation * relation = CreateConcatRelation(query->form, query->actors);
	size32 nTuples = 0;
	while(MixedTypeRelationNext(relation))
		nTuples++;
	FreeMixedTypeRelation(relation);
	FreeFormula(query);
	return nTuples;
}


/**
 * A query with a variable at every position must enumerate the whole relation,
 * and the tuples arrive in the actor order of the query.
 */
void testConcatEveryTuple(void)
{
	SetupEdgeFixture(&edgeFixture);
	Formula * query = CStringToTerm("edge _e from _x to _y");

	MixedTypeRelation * relation = CreateConcatRelation(query->form, query->actors);
	ASSERT_DATA64_EQUAL(relation->termForm.hash, query->form.hash)

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
	FreeFormula(query);
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
	Formula * query = CStringToTerm("edge _e from _x to _x");

	MixedTypeRelation * relation = CreateConcatRelation(query->form, query->actors);
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
	FreeFormula(query);

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
	Relation const * idRelation = CreateRelation(termForm, 2, (byte[]) {AT_ID, AT_ID});
	RelationTable * idTable = CreateRelationTable(
		idRelation, &btreeTableProvider, (index8[]) {0, 1});
	ReleaseRelation(idRelation);
	Relation const * uintRelation = CreateRelation(termForm, 2, (byte[]) {AT_ID, AT_UINT});
	RelationTable * uintTable = CreateRelationTable(
		uintRelation, &btreeTableProvider, (index8[]) {0, 1});
	ReleaseRelation(uintRelation);

	TypedAtom idActors[2] = {
		CreateTypedAtom(AT_ID, CreateStringFromCString("a")),
		CreateTypedAtom(AT_ID, CreateStringFromCString("b"))
	};
	TypedAtom uintActors[2] = {
		CreateTypedAtom(AT_ID, CreateStringFromCString("c")),
		CreateTypedAtom(AT_UINT, (Atom) {._uint = 42})
	};
	TypedTuple * idTuple = CreateTypedTupleFromArray(idActors, 2);
	TypedTuple * uintTuple = CreateTypedTupleFromArray(uintActors, 2);
	RelationTableAddTuple(idTable, TypedTuplePeekAtoms(idTuple), 0);
	RelationTableAddTuple(uintTable, TypedTuplePeekAtoms(uintTuple), 0);
	for(index8 i = 0; i < 2; i++) {
		ReleaseTypedAtom(idActors[i]);
		ReleaseTypedAtom(uintActors[i]);
	}

	Formula * query = CStringToTerm("first _x second _y");
	MixedTypeRelation * relation = CreateConcatRelation(query->form, query->actors);

	size32 nTuples = 0;
	bool foundIdTuple = false;
	bool foundUIntTuple = false;
	while(MixedTypeRelationNext(relation)) {
		TypedTuple const * tuple = MixedTypeRelationPeekTuple(relation);
		foundIdTuple = foundIdTuple || TypedTupleEqual(tuple, idTuple);
		foundUIntTuple = foundUIntTuple || TypedTupleEqual(tuple, uintTuple);
		nTuples++;
	}
	// One tuple of each relation, with the column types of the relation it came from
	ASSERT_UINT32_EQUAL(nTuples, 2)
	ASSERT_TRUE(foundIdTuple)
	ASSERT_TRUE(foundUIntTuple)

	FreeMixedTypeRelation(relation);
	FreeFormula(query);

	RelationTableRemoveTuple(idTable, TypedTuplePeekAtoms(idTuple), 0);
	RelationTableRemoveTuple(uintTable, TypedTuplePeekAtoms(uintTuple), 0);
	FreeTypedTuple(idTuple);
	FreeTypedTuple(uintTuple);
	DropRelationTable(uintTable);
	DropRelationTable(idTable);
	IFactRelease(termForm);
}


/**
 * A variable repeated across role where the underylying service has different atom types
 * should return no tuples. Here, for the service (list <ID position >UINT element >LETTER)
 * the constraint (list "ab" position x element x should yield no tuples.
 */
void testConcatRepeatedVariableAcrossTypes(void)
{
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" position _p element _e"), 2)
	ASSERT_UINT32_EQUAL(countQueryTuples("list \"ab\" position _x element _x"), 0)
}


/**
 * Constructing a MixedTypeRelation with a query that does not match any service
 * yields no tuples.
 */
void testConcatWithoutMatch(void)
{
	Formula * unknownQuery = CStringToTerm("nowhere _x nothing _y");
	MixedTypeRelation * relation = CreateConcatRelation(
		unknownQuery->form, unknownQuery->actors);
	ASSERT_FALSE(MixedTypeRelationNext(relation))
	// A relation read past its last tuple stays empty, and neither the dispatch
	// iterator nor a service is called again
	ASSERT_FALSE(MixedTypeRelationNext(relation))
	FreeMixedTypeRelation(relation);
	FreeFormula(unknownQuery);
}


/**
 * A relation may be freed at any position, so a caller that has seen the tuples it
 * wanted need not read the relation to its end. The service it was reading and the
 * dispatch iterator are released either way.
 */
void testConcatAbandonedIteration(void)
{
	SetupEdgeFixture(&edgeFixture);

	Formula * query = CStringToTerm("edge _e from _x to _y");
	MixedTypeRelation * relation = CreateConcatRelation(query->form, query->actors);
	ASSERT_TRUE(MixedTypeRelationNext(relation))
	FreeMixedTypeRelation(relation);
	FreeFormula(query);

	TeardownRelationFixture(&edgeFixture);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testConcatEveryTuple);
	ExecuteTest(testConcatRepeatedVariable);
	ExecuteTest(testConcatConstantQuery);
	ExecuteTest(testConcatAcrossRelations);
	ExecuteTest(testConcatRepeatedVariableAcrossTypes);
	ExecuteTest(testConcatWithoutMatch);
	ExecuteTest(testConcatAbandonedIteration);

	FreeMachineServices();
	KernelShutdown();
	TestSummary();
}
