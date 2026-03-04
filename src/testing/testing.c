

#include "testing/testing.h"
#include "kernel/ifact.h"

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
 * Execute a function and verify no references has changed.
 */
static void executeCheckReferences(void (*function)(void), char const * errorLabel)
{
	uint32 initialRefCount = TotalIFactReferenceCount();
	uint32 initialIFactCount = TotalIFactCount();

	function();

	uint32 refCountDiff = TotalIFactReferenceCount() - initialRefCount;
	if(refCountDiff < 0)
		PrintF("%s: Lost %u IFact references.\n", errorLabel, refCountDiff);
	if(refCountDiff > 0)
		PrintF("%s: Failed to release %u IFact references.\n", errorLabel, refCountDiff);

	uint32 ifactDiff = TotalIFactReferenceCount() - initialIFactCount;
	if(ifactDiff < 0)
		PrintF("%s: Lost %u IFacts.\n", errorLabel, ifactDiff);
	if(ifactDiff > 0)
		PrintF("%s: Failed to release %u IFacts.\n", errorLabel, ifactDiff);
}


void ExecuteTestSetupTearDown(void (*test)(void), void (*setup)(void), void (*teardown)(void))
{
	if(setup)
		executeCheckReferences(setup, "Setup");

	executeCheckReferences(test, "Test");

	if(teardown)
		executeCheckReferences(teardown, "Teardown");
}
