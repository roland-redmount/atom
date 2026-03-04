/**
 * A simple testing framework.
 * 
 * TODO: it should be possible to factor out some of this code to functions
 * to limit macro usage.
 */

#ifndef TESTING_H
#define TESTING_H

#include "platform.h"

#define MAX_NO_ERRORS 5


/**
 * Tests for various conditions, used by testing macros.
 */
void TestBool(
	char const * test_expr, bool test_value, bool expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestNull(
	char const * test_expr, void const * test_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestNotNull(
	char const * test_expr, void const * test_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestInt32(
	char const * test_expr, int32 test_value, int32 expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestUInt32(
	char const * test_expr, uint32 test_value, uint32 expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestInt64(
	char const * test_expr, int64 test_value, int64 expected_value,
	char const * functionName, char const * fileName, int32 lineNumber);

void TestUInt64(
	char const * test_expr, uint64 test_value, uint64 expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestFloat(
	char const * test_expr, float test_value, float expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestDouble(
	char const * test_expr, double test_value, double expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestChar(
	char const * test_expr, char test_value, char expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestString(
	char const * test_expr, char const * test_value, char const * expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestData64(
	char const * test_expr, data64 test_value, data64 expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestPtrEqual(
	char const * test_expr, void const * test_value, void const * expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestPtrNotEqual(
	char const * test_expr, void const * test_value, void const * expected_value,
	char const * functionName, char const * fileName, uint32 lineNumber);

void TestMemoryEqual(
	char const * test_expr, void const * test_address, void const * ref_address, size32 size,
	char const * functionName, char const * fileName, uint32 lineNumber);

/**
 * Print a test result summary.
 */
void TestSummary(void);


/**
 * Execute a test function and check for memory leaks
 */
void ExecuteTest(void (*test)(void));

/**
 * Execute a test function, with setup and teardown, and check for memory leaks
 */
void ExecuteTestSetupTearDown(void (*test)(void), void (*setup)(void), void (*teardown)(void));


#define ASSERT_TRUE(test_expr)\
	TestBool(#test_expr, test_expr, true, __func__, __FILE__, __LINE__);


#define ASSERT_FALSE(test_expr)\
	TestBool(#test_expr, test_expr, false, __func__, __FILE__, __LINE__);


#define ASSERT_NULL(test_expr)\
	TestNull(#test_expr, test_expr, __func__, __FILE__, __LINE__);


#define ASSERT_NOT_NULL(test_expr)\
	TestNotNull(#test_expr, test_expr, __func__, __FILE__, __LINE__);


#define ASSERT_INT32_EQUAL(test_expr, expected_expr)\
	TestInt32(#test_expr, test_expr, expected_expr, __func__, __FILE__, __LINE__);

	
#define ASSERT_UINT32_EQUAL(test_expr, expected_expr)\
	TestUInt32(#test_expr, test_expr, expected_expr, __func__, __FILE__, __LINE__);


#define ASSERT_INT64_EQUAL(test_expr, expected_expr)\
	TestInt64(#test_expr, test_expr, expected_expr, __func__, __FILE__, __LINE__);


#define ASSERT_UINT64_EQUAL(test_expr, expected_expr)\
	TestUInt64(#test_expr, test_expr, expected_expr, __func__, __FILE__, __LINE__);


#define ASSERT_FLOAT_EQUAL(test_expr, expected_expr)\
	TestFloat(#test_expr, test_expr, expected_expr, __func__, __FILE__, __LINE__);


#define ASSERT_DOUBLE_EQUAL(test_expr, expected_expr)\
	TestDouble(#test_expr, test_expr, expected_expr, __func__, __FILE__, __LINE__);


#define ASSERT_CHAR_EQUAL(test_expr, expected_expr)\
	TestChar(#test_expr, test_expr, expected_expr, __func__, __FILE__, __LINE__);


#define ASSERT_STRING_EQUAL(test_expr, expected_expr)\
	TestString(#test_expr, test_expr, expected_expr, __func__, __FILE__, __LINE__);


#define ASSERT_DATA64_EQUAL(test_expr, expected_expr)\
	TestData64(#test_expr, test_expr, expected_expr, __func__, __FILE__, __LINE__);


#define ASSERT_PTR_EQUAL(test_expr, expected_expr)\
	TestPtrEqual(#test_expr, test_expr, expected_expr, __func__, __FILE__, __LINE__);


#define ASSERT_PTR_NOT_EQUAL(test_expr, expected_expr)\
	TestPtrNotEqual(#test_expr, test_expr, expected_expr, __func__, __FILE__, __LINE__);


#define ASSERT_MEMORY_EQUAL(test_expr, expected_expr, size_expr)\
	TestMemoryEqual(#test_expr, test_expr, expected_expr, size_expr, __func__, __FILE__, __LINE__);


// TODO: ASSERT_SAME_ATOM()


#endif	// TESTING_H
