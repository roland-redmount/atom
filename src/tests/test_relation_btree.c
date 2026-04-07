
#include "datumtypes/Int.h"
#include "datumtypes/FloatIEEE754.h"
#include "datumtypes/Variable.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/RelationBTree.h"
#include "kernel/tuples.h"
#include "testing/testing.h"


// test fixture is defined by globals

#define TEST_N_COLUMNS	3

struct {
	BTree * tree;
	TypedAtom tuple1[TEST_N_COLUMNS];
	TypedAtom tuple2[TEST_N_COLUMNS];
	TypedAtom tuple3[TEST_N_COLUMNS];
	uint32 initialRefCount;
} fixture;


// NOTE: this table stores no DT_ID atoms, so these test do not
// cover reference handling

static void setupFixture(void)
{
	fixture.tree = CreateRelationBTree(TEST_N_COLUMNS);

	fixture.tuple1[0] = CreateInt(13);
	fixture.tuple1[1] = CreateFloat64(123.456);
	fixture.tuple1[2] = GetAlphabetLetter('A');

	fixture.tuple2[0] = fixture.tuple1[0];
	fixture.tuple2[1] = fixture.tuple1[1];
	fixture.tuple2[2] = GetAlphabetLetter('B');

	fixture.tuple3[0] = CreateInt(14);
	fixture.tuple3[1] = CreateFloat64(456.789);
	fixture.tuple3[2] = GetAlphabetLetter('C');
}

static void teardownFixture(void)
{
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

	TypedAtom queryTuple[TEST_N_COLUMNS];
	RelationBTreeIterator iterator;
	TypedAtom resultTuple[TEST_N_COLUMNS];
	TypedAtom tupleVariable1 = CreateVariable('x');
	TypedAtom tupleVariable2 = CreateVariable('y');
	
	// find tuple 1
	{
		CopyTuples(fixture.tuple1, queryTuple, TEST_N_COLUMNS);
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);

		ASSERT_TRUE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorGetTuple(&iterator, resultTuple);
		ASSERT_TRUE(SameTuples(resultTuple, fixture.tuple1, TEST_N_COLUMNS))
		RelationBTreeIteratorNext(&iterator);
		
		ASSERT_FALSE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorEnd(&iterator);
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
		queryTuple[0] = fixture.tuple1[0];
		queryTuple[1] =	fixture.tuple1[1];
		queryTuple[2] =	tupleVariable1;
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);

		ASSERT_TRUE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorGetTuple(&iterator, resultTuple);
		ASSERT_TRUE(SameTuples(resultTuple, fixture.tuple1, TEST_N_COLUMNS))
		RelationBTreeIteratorNext(&iterator);
		
		ASSERT_TRUE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorGetTuple(&iterator, resultTuple);
		ASSERT_TRUE(SameTuples(resultTuple, fixture.tuple2, TEST_N_COLUMNS))
		RelationBTreeIteratorNext(&iterator);
		
		ASSERT_FALSE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorEnd(&iterator);
	}

	// attempt to find non-matching tuple
	{
		queryTuple[0] = CreateInt(31);
		queryTuple[1] =	CreateFloat64(123.456);
		queryTuple[2] =	tupleVariable1;
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);
		
		ASSERT_FALSE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorEnd(&iterator);
	}

	// two variables, find tuple 3
	{
		queryTuple[0] = tupleVariable1;
		queryTuple[1] =	tupleVariable2;
		queryTuple[2] =	fixture.tuple3[2];
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);
		
		ASSERT_TRUE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorGetTuple(&iterator, resultTuple);
		ASSERT_TRUE(SameTuples(resultTuple, fixture.tuple3, TEST_N_COLUMNS))
		RelationBTreeIteratorNext(&iterator);
		
		ASSERT_FALSE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorEnd(&iterator);
	}

	// repeated variables
	{
		queryTuple[0] = CreateInt(13);
		queryTuple[1] =	tupleVariable1;
		queryTuple[2] =	tupleVariable1;
		RelationBTreeIterate(fixture.tree, queryTuple, &iterator);
		
		ASSERT_FALSE(RelationBTreeIteratorHasTuple(&iterator))
		RelationBTreeIteratorEnd(&iterator);
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
	TypedAtom queryTuple[TEST_N_COLUMNS];
	queryTuple[0] = fixture.tuple1[0];
	queryTuple[1] =	fixture.tuple1[1];
	queryTuple[2] =	anonymousVariable;
	
	nRemoved = RelationBTreeRemoveTuples(fixture.tree, queryTuple, REMOVE_NORMAL);

	ASSERT_UINT32_EQUAL(nRemoved, 2)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 1)

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
	TypedAtom queryTuple[TEST_N_COLUMNS];
	queryTuple[0] = anonymousVariable;
	queryTuple[1] =	anonymousVariable;
	queryTuple[2] =	anonymousVariable;

	size32 nRemoved = RelationBTreeRemoveTuples(fixture.tree, queryTuple, REMOVE_NORMAL);

	ASSERT_UINT32_EQUAL(nRemoved, 3)
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(fixture.tree), 0)

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
