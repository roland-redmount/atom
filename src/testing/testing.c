

#include "testing/testing.h"
#include "kernel/ifact.h"
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
 * NOTE: the below has a lot of repetition, but varying datum types
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


void TestSummary(void)
{
	if(testingFailCount == 0)
		PrintCString("OK\n");
	else
		PrintCString("FAIL\n");;
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

static void executeCheckReferences(void (*function)(void), CheckType checkType)
{
	uint32 initialRefCount;
	uint32 initialIFactCount;
	if(IFactsInitialized()) {
		initialRefCount = TotalIFactReferenceCount();
		initialIFactCount = TotalIFactCount();
	}
	uint32 initialBytesAllocated = AllocatorNBytesAllocated();

	function();
	
	if(IFactsInitialized()) {
		int32 refCountDiff = TotalIFactReferenceCount() - initialRefCount;
		if(checkType != CHECK_TEARDOWN && refCountDiff < 0)
			PrintF("%s: Lost %d IFact references.\n", checkTypeNames[checkType], refCountDiff);
		if(checkType != CHECK_SETUP && refCountDiff > 0)
			PrintF("%s: Failed to release %d IFact references.\n", checkTypeNames[checkType], refCountDiff);

		int32 ifactDiff = TotalIFactCount() - initialIFactCount;
		if(checkType != CHECK_TEARDOWN && ifactDiff < 0)
			PrintF("%s: Lost %d IFacts.\n", checkTypeNames[checkType], ifactDiff);
		if(checkType != CHECK_SETUP && ifactDiff > 0)
			PrintF("%s: Failed to release %d IFacts.\n", checkTypeNames[checkType], ifactDiff);
	}

	int32 allocateDiff = AllocatorNBytesAllocated() - initialBytesAllocated;
	if(checkType != CHECK_TEARDOWN && allocateDiff < 0)
		PrintF("%s: Lost %d allocated bytes.\n", checkTypeNames[checkType], allocateDiff);
	if(checkType != CHECK_SETUP && allocateDiff > 0)
		PrintF("%s: Failed to free %d allocated bytes.\n", checkTypeNames[checkType], allocateDiff);
}


void ExecuteTestSetupTearDown(void (*test)(void), void (*setup)(void), void (*teardown)(void))
{
	if(setup)
		executeCheckReferences(setup, CHECK_SETUP);
	executeCheckReferences(test, CHECK_TEST);
	if(teardown)
		executeCheckReferences(teardown, CHECK_TEARDOWN);
}
