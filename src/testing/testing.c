

#include "testing/testing.h"
#include "kernel/ifact.h"
#include "kernel/ServiceRegistry.h"
#include "lang/formula.h"
#include "memory/allocator.h"


static size32 testingFailCount = 0;
static size32 testingSuccessCount = 0;


static void logTest(bool condition)
{
	if(!(condition)) {
		testingFailCount++;
		if(testingFailCount == MAX_NO_ERRORS) {
			PrintCString("Max failure count exceeded.\n");
			AbortProgram();
		}
	}
	else
		testingSuccessCount++;
}


static char const * boolToString(bool x)
{
	return x ? "true" : "false";
}


static void printLocation(char const * functionName, char const * fileName, uint32 lineNumber)
{
	PrintF("\n   @ %s(), %s:%d.\n", functionName, fileName, lineNumber);
}


/**
 * NOTE: the below has a lot of repetition, but varying atom types
 * makes it difficult to extract out the pattern to a single function,
 * and I wanted to avoid macros as far as possible.
 */

void TestBool(
	char const * test_expr, bool test_value, bool expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value == expected_value);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = %s, expected %s", test_expr, boolToString(test_value), boolToString(expected_value));
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestNull(
	char const * test_expr, void const * test_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value == 0);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = %llx, expected null", test_expr, test_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestNotNull(
	char const * test_expr, void const * test_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value != 0);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s is null, expected non-null value", test_expr, test_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestInt32(
	char const * test_expr, int32 test_value, int32 expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value == expected_value);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = %d, expected %d", test_expr, test_value, expected_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestUInt32(
	char const * test_expr, uint32 test_value, uint32 expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value == expected_value);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = %u, expected %u", test_expr, test_value, expected_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestInt64(
	char const * test_expr, int64 test_value, int64 expected_value,
	char const * functionName, char const * fileName, int32 lineNumber)
{
	bool condition = (test_value == expected_value);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = %lld, expected %lld", test_expr, test_value, expected_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestUInt64(
	char const * test_expr, uint64 test_value, uint64 expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value == expected_value);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = %llu, expected %llu", test_expr, test_value, expected_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestFloat(
	char const * test_expr, float test_value, float expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value == expected_value);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = %f, expected %f", test_expr, test_value, expected_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestDouble(
	char const * test_expr, double test_value, double expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value == expected_value);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = %d, expected %d", test_expr, test_value, expected_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestChar(
	char const * test_expr, char test_value, char expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value == expected_value);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = '%c', expected '%c'", test_expr, test_value, expected_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestString(
	char const * test_expr, char const * test_value, char const * expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (CStringCompare(test_value, expected_value) == 0);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = \"%s\", expected \"%s\"", test_expr, test_value, expected_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestData64(
	char const * test_expr, data64 test_value, data64 expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value == expected_value);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = %llx, expected %llx", test_expr, test_value, expected_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestPtrEqual(
	char const * test_expr, void const * test_value, void const * expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value == expected_value);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = %llx, expected %llx", test_expr, (addr64) test_value, (addr64) expected_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}


void TestPtrNotEqual(
	char const * test_expr, void const * test_value, void const * expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	bool condition = (test_value != expected_value);
	if(!condition) {\
		PrintCString("FAIL ");
		PrintF("%s = %llx, expected != %llx", test_expr, (addr64) test_value, (addr64) expected_value);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(condition);
}

void TestMemoryEqual(
	char const * test_expr, void const * test_address, void const * ref_address, size32 size,
	char const * functionName, char const * fileName, uint32 lineNumber)
{
	uint32 result = CompareMemory(test_address, ref_address, size);
	if(result != 0) {\
		PrintCString("FAIL ");
		PrintF("Memory at %llx differs from expected at byte %u", test_address, result);
		printLocation(functionName, fileName, lineNumber);
	}
	logTest(result == 0);
}


int TestSummary(void)
{
	if(testingFailCount == 0) {
		PrintCString("OK\n");
		return 0;
	}
	else {
		PrintCString("FAIL\n");
		return 1;
	}
}


void ExecuteTest(void (*test)(void))
{
	ExecuteTestSetupTearDown(test, 0, 0);
}

/**
 * Execute a setup, teardown or test and verify references.
 * Tests may not alter any reference; Setup may only add references;
 * Teardown may only remove references.
 */

typedef enum {CHECK_SETUP = 0, CHECK_TEST = 1, CHECK_TEARDOWN = 2} CheckType;
const char * checkTypeNames[3] = {"Setup", "Test", "Teardown"};


/**
 * Report a failed reference check, such as "Lost 2 IFacts", as a failed test.
 */
static void failCheck(CheckType checkType, char const * verb, int32 difference, char const * noun)
{
	PrintF("FAIL %s: %s %d %s.\n", checkTypeNames[checkType], verb, difference, noun);
	logTest(false);
}


static void executeCheckReferences(void (*function)(void), CheckType checkType)
{
	/*
	* Compiled services are a cache, which may be removed at any time. If the service registry
	* is present, we remove services before and after executing the function. so that cached services
	* are not counted as allocation made by the function tested.
	*/
	if(ServiceRegistryInitialized())
		RemoveAllCompiledServices();

	// NOTE: the function may initialize IFacts, so we can only compare against
	// a baseline we actually took before calling it
	uint32 initialRefCount = 0;
	uint32 initialIFactCount = 0;
	uint32 initialFormulaRefCount = 0;
	uint32 initialFormulaCount = 0;
	// The formula registry is set up together with the ifacts; see KernelInitialize()
	bool ifactsInitialized = IFactsInitialized();
	if(ifactsInitialized) {
		initialRefCount = IFactTotalReferenceCount();
		initialIFactCount = IFactTotalCount();
		initialFormulaRefCount = FormulaTotalReferenceCount();
		initialFormulaCount = NumberOfFormulas();
		IFactsEnableFlagging();
	}
	uint32 initialBytesAllocated = AllocatorNBytesAllocated();

	function();

	if(ServiceRegistryInitialized())
		RemoveAllCompiledServices();

	if(ifactsInitialized) {
		int32 refCountDiff = IFactTotalReferenceCount() - initialRefCount;
		if(checkType != CHECK_TEARDOWN && refCountDiff < 0)
			failCheck(checkType, "Lost", refCountDiff, "IFact references");
		if(checkType != CHECK_SETUP && refCountDiff > 0)
			failCheck(checkType, "Failed to release", refCountDiff, "IFact references");

		int32 ifactDiff = IFactTotalCount() - initialIFactCount;
		if(checkType != CHECK_TEARDOWN && ifactDiff < 0)
			failCheck(checkType, "Lost", ifactDiff, "IFacts");
		if(checkType != CHECK_SETUP && ifactDiff > 0) {
			failCheck(checkType, "Failed to release", ifactDiff, "IFacts");
			PrintCString("Flagged IFacts:\n");
			IFactDumpFlagged();
		}
		IFactDisableFlagging();

		int32 formulaRefCountDiff = FormulaTotalReferenceCount() - initialFormulaRefCount;
		if(checkType != CHECK_TEARDOWN && formulaRefCountDiff < 0)
			failCheck(checkType, "Lost", formulaRefCountDiff, "formula references");
		if(checkType != CHECK_SETUP && formulaRefCountDiff > 0)
			failCheck(checkType, "Failed to release", formulaRefCountDiff, "formula references");

		int32 formulaDiff = NumberOfFormulas() - initialFormulaCount;
		if(checkType != CHECK_TEARDOWN && formulaDiff < 0)
			failCheck(checkType, "Lost", formulaDiff, "formulas");
		if(checkType != CHECK_SETUP && formulaDiff > 0) {
			failCheck(checkType, "Failed to release", formulaDiff, "formulas");
			FormulaDump();
		}
	}

	int32 allocateDiff = AllocatorNBytesAllocated() - initialBytesAllocated;
	if(checkType != CHECK_TEARDOWN && allocateDiff < 0)
		failCheck(checkType, "Lost", allocateDiff, "allocated bytes");
	if(checkType != CHECK_SETUP && allocateDiff > 0) {
		failCheck(checkType, "Failed to free", allocateDiff, "allocated bytes");
#ifdef DEBUG_ALLOCATE
		DumpAllocateLog();
#endif
	}

}


void ExecuteTestSetupTearDown(void (*test)(void), void (*setup)(void), void (*teardown)(void))
{
#ifdef DEBUG_ALLOCATE
	SetAllocationLogging(true);
#endif
	if(setup)
		executeCheckReferences(setup, CHECK_SETUP);
	executeCheckReferences(test, CHECK_TEST);
	if(teardown)
		executeCheckReferences(teardown, CHECK_TEARDOWN);
#ifdef DEBUG_ALLOCATE
	SetAllocationLogging(false);
#endif
}
