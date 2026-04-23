#include "kernel/kernel.h"
#include "kernel/tuple.h"
#include "testing/testing.h"


void testTupleSize(void)
{
	// test number of bytes per tuple is as expected
	for(index8 i = 1; i <= 6; i++) {
		size32 nBytes = TupleNBytes(i);
		ASSERT_UINT32_EQUAL(nBytes % 8, 0)
		ASSERT_UINT32_EQUAL(nBytes >> 3, i + 1)
	}
	for(index8 i = 7; i <= 14; i++) {
		size32 nBytes = TupleNBytes(i);
		ASSERT_UINT32_EQUAL(nBytes % 8, 0)
		ASSERT_UINT32_EQUAL(nBytes >> 3, i + 2)
	}
	for(index8 i = 15; i <= 22; i++) {
		size32 nBytes = TupleNBytes(i);
		ASSERT_UINT32_EQUAL(nBytes % 8, 0)
		ASSERT_UINT32_EQUAL(nBytes >> 3, i + 3)
	}

	// test that the inverse works
	for(index8 i = 1; i <= 22; i++) {
		size32 nBytes = TupleNBytes(i);
		size8 nAtoms = TupleNAtoms(nBytes);
		ASSERT_UINT32_EQUAL(nAtoms, i)
	}
}


int main(int argc, char * argv[])
{
	SetupMemory();

	ExecuteTest(testTupleSize);

	CleanupMemory();
	TestSummary();
}
