#include "btree/btree.h"
#include "kernel/Int.h"
#include "kernel/float.h"
#include "lang/Variable.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/RelationTable.h"
#include "kernel/RelationBTree.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
#include "testing/testing.h"


// test fixture is defined by globals

#define TEST_N_COLUMNS	3

struct {
	BTree * tree;
	Atom tuple1[TEST_N_COLUMNS];
	Atom tuple2[TEST_N_COLUMNS];
	Atom tuple3[TEST_N_COLUMNS];
	uint32 initialRefCount;
} fixture;


// NOTE: this table stores no AT_ID atoms, so these test do not
// cover reference handling

static void setupFixture(void)
{
	byte atomTypes[TEST_N_COLUMNS] = {AT_INT, AT_FLOAT, AT_LETTER} ;
	fixture.tree = CreateRelationBTree(TEST_N_COLUMNS, atomTypes);

	// C99 does not allow assigning array values
	CopyMemory(
		(Atom[]) {
			(Atom) {._int = 13},
			(Atom) {._float = 123.456},
			GetAlphabetLetter('A'),
		},
		fixture.tuple1,
		sizeof(fixture.tuple1)
	);
	CopyMemory(
		(Atom[]) {
			(Atom) {._int = 13},
			(Atom) {._float = 123.456},
			GetAlphabetLetter('B'),
		},
		fixture.tuple2,
		sizeof(fixture.tuple2)
	);
	CopyMemory(
		(Atom[]) {
			(Atom) {._int = 14},
			(Atom) {._float = 456.789},
			GetAlphabetLetter('C'),
		},
		fixture.tuple3,
		sizeof(fixture.tuple3)
	);
}

static void teardownFixture(void)
{
	BTreeFree(fixture.tree);
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

	RelationBTreeAddTuple(fixture.tree, fixture.tuple1, 0);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 1)

	RelationBTreeAddTuple(fixture.tree, fixture.tuple2, 0);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 2)

	// adding a tuple that exists should not change the table
	RelationBTreeAddTuple(fixture.tree, fixture.tuple1, 0);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 2)

	RelationBTreeAddTuple(fixture.tree, fixture.tuple3, 0);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 3)

	teardownFixture();
}


void testFindTuple(void)
{
	setupFixture();
	RelationBTreeAddTuple(fixture.tree, fixture.tuple1, 0);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple2, 0);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple3, 0);

	RelationBTreeIterator iterator;
	
	// find tuple 1
	{
		RelationBTreeIterate(fixture.tree, fixture.tuple1, TEST_N_COLUMNS, &iterator);

		ASSERT_TRUE(RelationBTreeIteratorNext(&iterator))
		Atom const * resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(TupleEqual(resultTuple, fixture.tuple1, TEST_N_COLUMNS))
		
		ASSERT_FALSE(RelationBTreeIteratorNext(&iterator))
		RelationBTreeIteratorEnd(&iterator);
	}

	// no query tuple, iterate over all 3 tuples
	{
		RelationBTreeIterate(fixture.tree, 0, 0, &iterator);
		size32 tupleCount = 0;
		while(RelationBTreeIteratorNext(&iterator)) {
			tupleCount++;
		}
		ASSERT_UINT32_EQUAL(tupleCount, 3)
		RelationBTreeIteratorEnd(&iterator);
	}

	// query with 2 leading inputs, matching tuples 1 and 2
	{
		Atom queryTuple[3] = {
			fixture.tuple1[0],
			fixture.tuple1[1],
			(Atom) {0}
		};
		RelationBTreeIterate(fixture.tree, queryTuple, 2, &iterator);

		ASSERT_TRUE(RelationBTreeIteratorNext(&iterator))
		Atom const * resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(TupleEqual(resultTuple, fixture.tuple1, TEST_N_COLUMNS))
		
		ASSERT_TRUE(RelationBTreeIteratorNext(&iterator))
		resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(TupleEqual(resultTuple, fixture.tuple2, TEST_N_COLUMNS))
		
		ASSERT_FALSE(RelationBTreeIteratorNext(&iterator))
		RelationBTreeIteratorEnd(&iterator);
	}

	// query with no matching tuple
	{
		Atom queryTuple[3] = {
			(Atom) {._int = 31},
			(Atom) {._float = 123.456},
			GetAlphabetLetter('X'),
		};
		RelationBTreeIterate(fixture.tree, queryTuple, TEST_N_COLUMNS, &iterator);
		ASSERT_FALSE(RelationBTreeIteratorNext(&iterator))
		RelationBTreeIteratorEnd(&iterator);
	}

	// NOTE: queries with repeated variable (equality constraint) is not possible,
	// requires a higher-level mechanism to filter out matching tuples.

	teardownFixture();
}


void testRemoveTuple(void)
{
	setupFixture();

	RelationBTreeAddTuple(fixture.tree, fixture.tuple1, 0);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple2, 0);
	RelationBTreeAddTuple(fixture.tree, fixture.tuple3, 0);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 3)

	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuple(fixture.tree, fixture.tuple2), TUPLE_REMOVED)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 2)

	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuple(fixture.tree, fixture.tuple3), TUPLE_REMOVED)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 1)

	// attempting to remove a tuple that does not exist 
	// does not change the number of rows
	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuple(fixture.tree, fixture.tuple2), TUPLE_NOT_FOUND)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 1)

	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuple(fixture.tree, fixture.tuple1), TUPLE_REMOVED)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 0)

	// attempt to remove from empty tree
	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuple(fixture.tree, fixture.tuple1), TUPLE_NOT_FOUND)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 0)

	teardownFixture();
}

// NOTE: relation tables currently do not support removing multiple tuples using variables;
// this requires a service to handle the search. 

// void testRemoveTuples(void)
// {
// 	setupFixture();

// 	RelationBTreeAddTuple(fixture.tree, fixture.tuple1, 0);
// 	RelationBTreeAddTuple(fixture.tree, fixture.tuple2, 0);
// 	RelationBTreeAddTuple(fixture.tree, fixture.tuple3, 0);
// 	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 3)

// 	size32 nRemoved;

// 	// remove tuple 1 and 2
// 	TypedTuple * queryTuple = CreateTypedTupleFromArray(
// 		(TypedAtom[]) {
// 			TypedTupleGetElement(fixture.tuple1, 0),
// 			TypedTupleGetElement(fixture.tuple1, 1),
// 			anonymousVariable,
// 		},
// 		TEST_N_COLUMNS
// 	);
// 	nRemoved = RelationBTreeRemoveTuples(fixture.tree, queryTuple);
// 	ASSERT_UINT32_EQUAL(nRemoved, 2)
// 	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 1)
// 	FreeTypedTuple(queryTuple);

// 	// remove tuple 3
// 	nRemoved = RelationBTreeRemoveTuples(fixture.tree, fixture.tuple3);
// 	ASSERT_UINT32_EQUAL(nRemoved, 1)
// 	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 0)

// 	teardownFixture();
// }


// void testRemoveAllTuples(void)
// {
// 	setupFixture();

// 	RelationBTreeAddTuple(fixture.tree, fixture.tuple1);
// 	RelationBTreeAddTuple(fixture.tree, fixture.tuple2);
// 	RelationBTreeAddTuple(fixture.tree, fixture.tuple3);
// 	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 3)

// 	// query tuple matching any tuple
// 	TypedTuple * queryTuple = CreateTypedTupleFromArray(
// 		(TypedAtom[]) {
// 			anonymousVariable,
// 			anonymousVariable,
// 			anonymousVariable,
// 		},
// 		TEST_N_COLUMNS
// 	);
// 	size32 nRemoved = RelationBTreeRemoveTuples(fixture.tree, queryTuple, REMOVE_NORMAL);
// 	ASSERT_UINT32_EQUAL(nRemoved, 3)
// 	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 0)
// 	FreeTypedTuple(queryTuple);

// 	teardownFixture();
// }


int main(void)
{
	// NOTE: this does not use the kernel, only memory allocation
	SetupMemory();

	ExecuteTest(testCreateRelationTable);
	ExecuteTest(testAddTuple);
	ExecuteTest(testFindTuple);
	ExecuteTest(testRemoveTuple);
	// ExecuteTest(testRemoveTuples);
	// ExecuteTest(testRemoveAllTuples);
	
	TestSummary();

	CleanupMemory();
}
