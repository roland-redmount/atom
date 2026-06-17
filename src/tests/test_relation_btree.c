
#include "kernel/Int.h"
#include "kernel/float.h"
#include "lang/Variable.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/RelationBTree.h"
#include "kernel/typedtuple.h"
#include "testing/testing.h"


// test fixture is defined by globals

#define TEST_N_COLUMNS	3

struct {
	BTree * tree;
	TypedTuple * tuple1;
	TypedTuple * tuple2;
	TypedTuple * tuple3;
	uint32 initialRefCount;
} fixture;


// NOTE: this table stores no AT_ID atoms, so these test do not
// cover reference handling

static void setupFixture(void)
{
	fixture.tree = CreateRelationBTree(TEST_N_COLUMNS);

	fixture.tuple1 = CreateTypedTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_INT, (Atom) {._int = 13 }),
			CreateTypedAtom(AT_FLOAT, (Atom) {._float = 123.456}),
			CreateTypedAtom(AT_LETTER, GetAlphabetLetter('A')),
		},
		TEST_N_COLUMNS
	);
	fixture.tuple2 = CreateTypedTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_INT, (Atom) {._int = 13 }),
			CreateTypedAtom(AT_FLOAT, (Atom) {._float = 123.456}),
			CreateTypedAtom(AT_LETTER, GetAlphabetLetter('B')),
		},
		TEST_N_COLUMNS
	);
	fixture.tuple3 = CreateTypedTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_UINT, (Atom) {._int = 14 }),
			CreateTypedAtom(AT_FLOAT, (Atom) {._float = 456.789}),
			CreateTypedAtom(AT_LETTER, GetAlphabetLetter('C')),
		},
		TEST_N_COLUMNS
	);
}

static void teardownFixture(void)
{
	FreeTypedTuple(fixture.tuple1);
	FreeTypedTuple(fixture.tuple2);
	FreeTypedTuple(fixture.tuple3);
	FreeRelationBTree(fixture.tree);
}


void testCreateRelationTable(void)
{
	setupFixture();

	ASSERT_UINT32_EQUAL(RelationBTreeNColumns(fixture.tree), TEST_N_COLUMNS)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 0)

	teardownFixture();
}


void testAddTuple(void )
{
	setupFixture();

	RelationBTreeAddTuple(fixture.tree, fixture.tuple1);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 1)

	RelationBTreeAddTuple(fixture.tree, fixture.tuple2);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 2)

	// adding a tuple that exists should not change the table
	RelationBTreeAddTuple(fixture.tree, fixture.tuple1);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 2)

	RelationBTreeAddTuple(fixture.tree, fixture.tuple3);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 3)

	teardownFixture();
}


void testFindTuple(void)
{
	setupFixture();
	RelationBTreeAddTuple(fixture.tree, fixture.tuple1);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple2);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple3);

	RelationBTreeIterator iterator;
	
	// find tuple 1
	{
		TypedTuple * queryTuple = CreateTypedTuple(3);
		TypedTupleCopy(fixture.tuple1, queryTuple);
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);

		ASSERT_TRUE(RelationBTreeIteratorNext(&iterator))
		TypedTuple const * resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(TypedTupleEqual(resultTuple, fixture.tuple1))
		
		ASSERT_FALSE(RelationBTreeIteratorNext(&iterator))
		RelationBTreeIteratorEnd(&iterator);
		FreeTypedTuple(queryTuple);
	}

	// no query tuple, iterate over all 3 tuples
	{
		RelationBTreeIterate(fixture.tree, 0, &iterator);
		size32 tupleCount = 0;
		while(RelationBTreeIteratorNext(&iterator)) {
			tupleCount++;
		}
		ASSERT_UINT32_EQUAL(tupleCount, 3)
		RelationBTreeIteratorEnd(&iterator);
	}

	// query matching tuples 1 and 2
	{
		TypedTuple * queryTuple = CreateTypedTuple(3);
		TypedTupleCopy(fixture.tuple1, queryTuple);
		TypedTupleSetElement(queryTuple, 2, CreateTypedAtom(AT_VARIABLE, CreateVariable('x')));
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);

		ASSERT_TRUE(RelationBTreeIteratorNext(&iterator))
		TypedTuple const * resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(TypedTupleEqual(resultTuple, fixture.tuple1))
		
		ASSERT_TRUE(RelationBTreeIteratorNext(&iterator))
		resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(TypedTupleEqual(resultTuple, fixture.tuple2))
		
		ASSERT_FALSE(RelationBTreeIteratorNext(&iterator))
		RelationBTreeIteratorEnd(&iterator);
		FreeTypedTuple(queryTuple);
	}

	// query with no matching tuple
	{
		TypedTuple * queryTuple = CreateTypedTupleFromArray(
			(TypedAtom[]) {
				CreateTypedAtom(AT_INT, (Atom) {._int = 31}),
				CreateTypedAtom(AT_FLOAT, (Atom) {._float = 123.456}),
				CreateTypedAtom(AT_VARIABLE, CreateVariable('x')),
			},
			TEST_N_COLUMNS
		);
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);
		ASSERT_FALSE(RelationBTreeIteratorNext(&iterator))
		RelationBTreeIteratorEnd(&iterator);
		FreeTypedTuple(queryTuple);
	}

	// query with two variables, find tuple 3
	{
		TypedTuple * queryTuple = CreateTypedTupleFromArray(
			(TypedAtom[]) {
				CreateTypedAtom(AT_VARIABLE, CreateVariable('x')),
				CreateTypedAtom(AT_VARIABLE, CreateVariable('y')),
				TypedTupleGetElement(fixture.tuple3, 2),
			},
			TEST_N_COLUMNS
		);
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);
		
		ASSERT_TRUE(RelationBTreeIteratorNext(&iterator))
		TypedTuple const * resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(TypedTupleEqual(resultTuple, fixture.tuple3))
		
		ASSERT_FALSE(RelationBTreeIteratorNext(&iterator))
		RelationBTreeIteratorEnd(&iterator);
		FreeTypedTuple(queryTuple);
	}

	// query with repeated variable (equality constraint)
	{
		TypedTuple * queryTuple = CreateTypedTupleFromArray(
			(TypedAtom[]) {
				CreateTypedAtom(AT_INT, (Atom) {._int = 31}),
				CreateTypedAtom(AT_VARIABLE, CreateVariable('x')),
				CreateTypedAtom(AT_VARIABLE, CreateVariable('x')),
			},
			TEST_N_COLUMNS
		);
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);	
		ASSERT_FALSE(RelationBTreeIteratorNext(&iterator))
		RelationBTreeIteratorEnd(&iterator);
		FreeTypedTuple(queryTuple);
	}
	
	// query with typed variable
	{
		TypedTuple * queryTuple = CreateTypedTupleFromArray(
			(TypedAtom[]) {
				CreateTypedAtom(AT_VARIABLE, CreateTypedVariable('x', AT_UINT)),
				anonymousVariable,
				anonymousVariable,
			},
			TEST_N_COLUMNS
		);
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);
		
		ASSERT_TRUE(RelationBTreeIteratorNext(&iterator))
		TypedTuple const * resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(TypedTupleEqual(resultTuple, fixture.tuple3))
		
		ASSERT_FALSE(RelationBTreeIteratorNext(&iterator))
		RelationBTreeIteratorEnd(&iterator);
		FreeTypedTuple(queryTuple);
	}

	teardownFixture();
}


void testRemoveTuple(void)
{
	setupFixture();

	RelationBTreeAddTuple(fixture.tree, fixture.tuple1);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple2);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple3);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 3)

	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuples(fixture.tree, fixture.tuple2, REMOVE_NORMAL), 1)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 2)

	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuples(fixture.tree, fixture.tuple3, REMOVE_NORMAL), 1)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 1)

	// attempting to remove a tuple that does not exist 
	// does not change the number of rows
	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuples(fixture.tree, fixture.tuple2, REMOVE_NORMAL), 0)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 1)

	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuples(fixture.tree, fixture.tuple1, REMOVE_NORMAL), 1)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 0)

	// attempt to remove from empty tree
	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuples(fixture.tree, fixture.tuple1, REMOVE_NORMAL), 0)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 0)

	teardownFixture();
}


void testRemoveTuples(void)
{
	setupFixture();

	RelationBTreeAddTuple(fixture.tree, fixture.tuple1);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple2);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple3);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 3)

	size32 nRemoved;

	// remove tuple 1 and 2
	TypedTuple * queryTuple = CreateTypedTupleFromArray(
		(TypedAtom[]) {
			TypedTupleGetElement(fixture.tuple1, 0),
			TypedTupleGetElement(fixture.tuple1, 1),
			anonymousVariable,
		},
		TEST_N_COLUMNS
	);
	nRemoved = RelationBTreeRemoveTuples(fixture.tree, queryTuple, REMOVE_NORMAL);
	ASSERT_UINT32_EQUAL(nRemoved, 2)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 1)
	FreeTypedTuple(queryTuple);

	// remove tuple 3
	nRemoved = RelationBTreeRemoveTuples(fixture.tree, fixture.tuple3, REMOVE_NORMAL);
	ASSERT_UINT32_EQUAL(nRemoved, 1)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 0)

	teardownFixture();
}



void testRemoveAllTuples(void)
{
	setupFixture();

	RelationBTreeAddTuple(fixture.tree, fixture.tuple1);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple2);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple3);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 3)

	// query tuple matching any tuple
	TypedTuple * queryTuple = CreateTypedTupleFromArray(
		(TypedAtom[]) {
			anonymousVariable,
			anonymousVariable,
			anonymousVariable,
		},
		TEST_N_COLUMNS
	);
	size32 nRemoved = RelationBTreeRemoveTuples(fixture.tree, queryTuple, REMOVE_NORMAL);
	ASSERT_UINT32_EQUAL(nRemoved, 3)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 0)
	FreeTypedTuple(queryTuple);

	teardownFixture();
}


int main(void)
{
	// NOTE: this does not use the kernel, only memory allocation
	SetupMemory();

	ExecuteTest(testCreateRelationTable);
	ExecuteTest(testAddTuple);
	ExecuteTest(testFindTuple);
	ExecuteTest(testRemoveTuple);
	ExecuteTest(testRemoveTuples);
	ExecuteTest(testRemoveAllTuples);
	
	TestSummary();

	CleanupMemory();
}
