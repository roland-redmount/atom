
#include "kernel/Int.h"
#include "kernel/FloatIEEE754.h"
#include "lang/Variable.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/RelationBTree.h"
#include "kernel/tuple.h"
#include "testing/testing.h"


// test fixture is defined by globals

#define TEST_N_COLUMNS	3

struct {
	BTree * tree;
	Tuple * tuple1;
	Tuple * tuple2;
	Tuple * tuple3;
	uint32 initialRefCount;
} fixture;


// NOTE: this table stores no AT_ID atoms, so these test do not
// cover reference handling

static void setupFixture(void)
{
	fixture.tree = CreateRelationBTree(TEST_N_COLUMNS);

	fixture.tuple1 = CreateTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_INT, 13),
			CreateTypedAtom(AT_FLOAT64, CreateFloat64(123.456)),
			GetAlphabetLetter('A'),
		},
		TEST_N_COLUMNS
	);
	fixture.tuple2 = CreateTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_INT, 13),
			CreateTypedAtom(AT_FLOAT64, CreateFloat64(123.456)),
			GetAlphabetLetter('B'),
		},
		TEST_N_COLUMNS
	);
	fixture.tuple3 = CreateTupleFromArray(
		(TypedAtom[]) {
			CreateTypedAtom(AT_INT, 14),
			CreateTypedAtom(AT_FLOAT64, CreateFloat64(456.789)),
			GetAlphabetLetter('C'),
		},
		TEST_N_COLUMNS
	);
}

static void teardownFixture(void)
{
	FreeTuple(fixture.tuple1);
	FreeTuple(fixture.tuple2);
	FreeTuple(fixture.tuple3);
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
		Tuple * queryTuple = CreateTuple(3);
		CopyTuples(fixture.tuple1, queryTuple);
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);

		ASSERT_TRUE(RelationBTreeIteratorHasTuple(&iterator))
		Tuple const * resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(SameTuples(resultTuple, fixture.tuple1))
		RelationBTreeIteratorNext(&iterator);
		
		ASSERT_FALSE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorEnd(&iterator);
		FreeTuple(queryTuple);
	}

	// no query tuple, iterate over all 3 tuples
	{
		RelationBTreeIterate(fixture.tree, 0, &iterator);
		size32 tupleCount = 0;
		while(RelationBTreeIteratorHasTuple(&iterator)) {
			tupleCount++;
			RelationBTreeIteratorNext(&iterator);
		}
		ASSERT_UINT32_EQUAL(tupleCount, 3)
		RelationBTreeIteratorEnd(&iterator);
	}

	// find tuples 1 and 2
	{
		Tuple * queryTuple = CreateTuple(3);
		CopyTuples(fixture.tuple1, queryTuple);
		TupleSetElement(queryTuple, 2, CreateVariable('x'));
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);

		ASSERT_TRUE(RelationBTreeIteratorHasTuple(&iterator))
		Tuple const * resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(SameTuples(resultTuple, fixture.tuple1))
		RelationBTreeIteratorNext(&iterator);
		
		ASSERT_TRUE(RelationBTreeIteratorHasTuple(&iterator))
		resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(SameTuples(resultTuple, fixture.tuple2))
		RelationBTreeIteratorNext(&iterator);
		
		ASSERT_FALSE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorEnd(&iterator);
		FreeTuple(queryTuple);
	}

	// attempt to find non-matching tuple
	{
		Tuple * queryTuple = CreateTupleFromArray(
			(TypedAtom[]) {
				CreateTypedAtom(AT_INT, 31),
				CreateTypedAtom(AT_FLOAT64, CreateFloat64(123.456)),
				CreateVariable('x'),
			},
			TEST_N_COLUMNS
		);
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);
		ASSERT_FALSE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorEnd(&iterator);
		FreeTuple(queryTuple);
	}

	// two variables, find tuple 3
	{
		Tuple * queryTuple = CreateTupleFromArray(
			(TypedAtom[]) {
				CreateVariable('x'),
				CreateVariable('y'),
				TupleGetElement(fixture.tuple3, 2),
			},
			TEST_N_COLUMNS
		);
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);
		
		ASSERT_TRUE(RelationBTreeIteratorHasTuple(&iterator))
		Tuple const * resultTuple = RelationBTreeIteratorPeekTuple(&iterator);
		ASSERT_TRUE(SameTuples(resultTuple, fixture.tuple3))
		RelationBTreeIteratorNext(&iterator);
		
		ASSERT_FALSE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorEnd(&iterator);
		FreeTuple(queryTuple);
	}

	// repeated variables (equality constraint)
	{
		Tuple * queryTuple = CreateTupleFromArray(
			(TypedAtom[]) {
				CreateTypedAtom(AT_INT, 13),
				CreateVariable('x'),
				CreateVariable('x'),
			},
			TEST_N_COLUMNS
		);
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);	
		ASSERT_FALSE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorEnd(&iterator);
		FreeTuple(queryTuple);
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
	Tuple * queryTuple = CreateTupleFromArray(
		(TypedAtom[]) {
			TupleGetElement(fixture.tuple1, 0),
			TupleGetElement(fixture.tuple1, 1),
			anonymousVariable,
		},
		TEST_N_COLUMNS
	);
	nRemoved = RelationBTreeRemoveTuples(fixture.tree, queryTuple, REMOVE_NORMAL);
	ASSERT_UINT32_EQUAL(nRemoved, 2)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 1)
	FreeTuple(queryTuple);

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
	Tuple * queryTuple = CreateTupleFromArray(
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
	FreeTuple(queryTuple);

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
