
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/MixedTypeRelation.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/MachineService.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


// A directed graph (edge:ID from:ID to:ID), two of whose edges are self edges
#define TEST_N_EDGES	4

static struct {
	Atom form;
	RelationTable const * table;
	index8 edgeIndex;
	index8 fromIndex;
	index8 toIndex;
	TypedTuple * tuples[TEST_N_EDGES];
} edgeFixture;


/**
 * Assert facts of the form (edge e from a to b) where all atoms are AT_ID.
 */
static void setupEdgeFixture(void)
{
	Atom roles[3] = {
		CreateNameFromCString("edge"),
		CreateNameFromCString("from"),
		CreateNameFromCString("to")
	};
	Atom predicateForm = CreatePredicateForm(roles, 3);
	edgeFixture.form = CreateTermForm(predicateForm, true);
	edgeFixture.edgeIndex = PredicateRoleIndex(predicateForm, roles[0]);
	edgeFixture.fromIndex = PredicateRoleIndex(predicateForm, roles[1]);
	edgeFixture.toIndex = PredicateRoleIndex(predicateForm, roles[2]);
	IFactRelease(predicateForm);
	for(index8 i = 0; i < 3; i++)
		NameRelease(roles[i]);

	byte atomTypes[3] = {AT_ID, AT_ID, AT_ID};
	edgeFixture.table = CreateRelationBTreeWithServices(
		edgeFixture.form, 3, atomTypes, (index8[]) {0, 1, 2});

	// The graph a -> b, a -> a, b -> b, b -> c, so eq and er are the self edges
	char const * edgeNames[TEST_N_EDGES] = {"ep", "eq", "er", "es"};
	char const * fromNames[TEST_N_EDGES] = {"a", "a", "b", "b"};
	char const * toNames[TEST_N_EDGES] = {"b", "a", "b", "c"};
	for(index8 i = 0; i < TEST_N_EDGES; i++) {
		TypedAtom actors[3];
		actors[edgeFixture.edgeIndex] = CreateTypedAtom(AT_ID, CreateStringFromCString(edgeNames[i]));
		actors[edgeFixture.fromIndex] = CreateTypedAtom(AT_ID, CreateStringFromCString(fromNames[i]));
		actors[edgeFixture.toIndex] = CreateTypedAtom(AT_ID, CreateStringFromCString(toNames[i]));
		edgeFixture.tuples[i] = CreateTypedTupleFromArray(actors, 3);
		// the relation table now holds a reference to each atom
		AssertFact(edgeFixture.form, edgeFixture.tuples[i], 0);
		for(index8 j = 0; j < 3; j++)
			ReleaseTypedAtom(actors[j]);
	}
}


static void teardownEdgeFixture(void)
{
	// the relation table must be empty before it can be removed
	for(index8 i = 0; i < TEST_N_EDGES; i++) {
		RetractFact(edgeFixture.form, edgeFixture.tuples[i]);
		FreeTypedTuple(edgeFixture.tuples[i]);
	}
	ServiceRegistryRemoveAll(edgeFixture.table);
	RelationRegistryRemove(edgeFixture.table);
	IFactRelease(edgeFixture.form);
}


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
	setupEdgeFixture();
	Formula * query = CStringToTerm("edge _e from _x to _y");

	MixedTypeRelation * relation = CreateConcatRelation(query->form, query->actors);
	ASSERT_DATA64_EQUAL(relation->termForm.hash, query->form.hash)

	// Each tuple of the relation arrives once, though we do not know in which order,
	// as the relation is stored sorted by atom
	bool foundTuple[TEST_N_EDGES] = {false, false, false, false};
	size32 nTuples = 0;
	while(MixedTypeRelationNext(relation)) {
		TypedTuple const * tuple = MixedTypeRelationPeekTuple(relation);
		ASSERT_UINT32_EQUAL(tuple->nAtoms, 3)
		for(index8 i = 0; i < 3; i++)
			ASSERT_UINT32_EQUAL(TypedTupleGetElement(tuple, i).type, AT_ID)
		for(index8 i = 0; i < TEST_N_EDGES; i++) {
			if(TypedTupleEqual(tuple, edgeFixture.tuples[i])) {
				ASSERT_FALSE(foundTuple[i])
				foundTuple[i] = true;
			}
		}
		nTuples++;
	}
	ASSERT_UINT32_EQUAL(nTuples, TEST_N_EDGES)
	for(index8 i = 0; i < TEST_N_EDGES; i++)
		ASSERT_TRUE(foundTuple[i])

	FreeMixedTypeRelation(relation);
	FreeFormula(query);
	teardownEdgeFixture();
}


/**
 * A variable occurring twice in a query constrains the two positions to be equal.
 * Here, the query (edge e from x to x) asks for the self-edges. Since the service
 * this dispatches to enumerates every edge, MixedTypeRelation must filter the tuples.
 */
void testConcatRepeatedVariable(void)
{
	setupEdgeFixture();
	Formula * query = CStringToTerm("edge _e from _x to _x");

	MixedTypeRelation * relation = CreateConcatRelation(query->form, query->actors);
	size32 nTuples = 0;
	while(MixedTypeRelationNext(relation)) {
		TypedTuple const * tuple = MixedTypeRelationPeekTuple(relation);
		ASSERT_DATA64_EQUAL(
			TypedTupleGetAtom(tuple, edgeFixture.fromIndex).hash,
			TypedTupleGetAtom(tuple, edgeFixture.toIndex).hash)
		nTuples++;
	}
	ASSERT_UINT32_EQUAL(nTuples, 2)
	FreeMixedTypeRelation(relation);
	FreeFormula(query);

	// Each occurence of the anonymous variable is a variable of its own, and so
	// constrains nothing
	ASSERT_UINT32_EQUAL(countQueryTuples("edge _ from _ to _"), TEST_N_EDGES)

	teardownEdgeFixture();
}


/**
 * Test MixedTypeRelation on queries with all inputs, yielding 1 or 0 tuples.
 */
void testConcatConstantQuery(void)
{
	setupEdgeFixture();

	// The edge eq is the self edge of a
	ASSERT_UINT32_EQUAL(countQueryTuples("edge \"eq\" from \"a\" to \"a\""), 1)
	ASSERT_UINT32_EQUAL(countQueryTuples("edge \"eq\" from \"a\" to \"b\""), 0)

	teardownEdgeFixture();
}


/**
 * Test MixedTypeRelation with two underlying services having distinct atom types.
 * This requires concatenating tuples from the two relations.
 */
void testConcatAcrossRelations(void)
{
	Atom roles[2] = {
		CreateNameFromCString("first"),
		CreateNameFromCString("second")
	};
	Atom predicateForm = CreatePredicateForm(roles, 2);
	Atom termForm = CreateTermForm(predicateForm, true);
	IFactRelease(predicateForm);
	for(index8 i = 0; i < 2; i++)
		NameRelease(roles[i]);

	// Two relation tables for the term form, one per combination of column types
	RelationTable const * idTable = CreateRelationBTreeWithServices(
		termForm, 2, (byte[]) {AT_ID, AT_ID}, (index8[]) {0, 1});
	RelationTable const * uintTable = CreateRelationBTreeWithServices(
		termForm, 2, (byte[]) {AT_ID, AT_UINT}, (index8[]) {0, 1});

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
	AssertFact(termForm, idTuple, 0);
	AssertFact(termForm, uintTuple, 0);
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

	RetractFact(termForm, idTuple);
	RetractFact(termForm, uintTuple);
	FreeTypedTuple(idTuple);
	FreeTypedTuple(uintTuple);
	ServiceRegistryRemoveAll(uintTable);
	RelationRegistryRemove(uintTable);
	ServiceRegistryRemoveAll(idTable);
	RelationRegistryRemove(idTable);
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
	setupEdgeFixture();

	Formula * query = CStringToTerm("edge _e from _x to _y");
	MixedTypeRelation * relation = CreateConcatRelation(query->form, query->actors);
	ASSERT_TRUE(MixedTypeRelationNext(relation))
	FreeMixedTypeRelation(relation);
	FreeFormula(query);

	teardownEdgeFixture();
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
