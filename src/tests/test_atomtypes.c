
#include "kernel/float.h"
#include "kernel/Int.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "testing/testing.h"


void testGetAtomTypeName(void)
{
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_INT), "INT")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_FLOAT), "FLOAT")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_LETTER), "LETTER")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_VARIABLE), "VARIABLE")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_NAME), "NAME")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_ID), "ID")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_FORMULA), "FORMULA")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_GENERATOR), "GENERATOR")
}


void testAtomTypeFromString(void)
{
	ASSERT_UINT32_EQUAL(AtomTypeFromString("NAME", 4), AT_NAME)
	ASSERT_UINT32_EQUAL(AtomTypeFromString("FORMULA", 7), AT_FORMULA)
	ASSERT_UINT32_EQUAL(AtomTypeFromString("PARAMETER", 9), AT_PARAMETER)
	// an unknown name is no type
	ASSERT_UINT32_EQUAL(AtomTypeFromString("FROB", 4), 0)
}


static void testLetter(void)
{
	index8 i = 1;
	for(char c = 'A'; c <= 'Z'; c++) {
		Atom letter = CreateLetter(c);
		ASSERT_DATA64_EQUAL(letter.letter.code, i)
		i++;
	}

	i = 1;
	for(char c = 'a'; c <= 'z'; c++) {
		Atom letter = CreateLetter(c);
		ASSERT_DATA64_EQUAL(letter.letter.code, i)
		i++;
	}
}


/*
 * CLAUDE: Format a float with FormatFloat() and compare with the expected string.
 */
static void assertFloatFormat(double value, char const * expected)
{
	char buffer[FLOAT_FORMAT_BUFFER_SIZE];
	FormatFloat(value, buffer, FLOAT_FORMAT_BUFFER_SIZE);
	ASSERT_STRING_EQUAL(buffer, expected)
}


/**
 * CLAUDE: A float whose 6 decimals are exact is written with no trailing zeros, keeping one
 * decimal. Any other finite float is written with 6 decimals followed by "..".
 */
void testFormatFloat(void)
{
	assertFloatFormat(1.5, "1.5");
	assertFloatFormat(-1.5, "-1.5");
	assertFloatFormat(2.0, "2.0");
	assertFloatFormat(0.0, "0.0");
	assertFloatFormat(0.015625, "0.015625");
	assertFloatFormat(1e20, "100000000000000000000.0");
	// Rounded to 6 decimals
	assertFloatFormat(1.4999999, "1.500000..");
	assertFloatFormat(0.1, "0.100000..");
	assertFloatFormat(0.0078125, "0.007812..");
	assertFloatFormat(3.14, "3.140000..");
	assertFloatFormat(-3.14, "-3.140000..");
	// Neither exact nor rounded
	assertFloatFormat(1e308 * 10.0, "inf");
	assertFloatFormat(-1e308 * 10.0, "-inf");

	// The most negative double fills the buffer most: the sign, 309 digits and ".0"
	char buffer[FLOAT_FORMAT_BUFFER_SIZE];
	ASSERT_UINT32_EQUAL(FormatFloat(-1.7976931348623157e308, buffer, FLOAT_FORMAT_BUFFER_SIZE), 312)
}


int main(int argc, char * argv[])
{
	SetupMemory(TRANSIENT_MEMORY);

	ExecuteTest(testGetAtomTypeName);
	ExecuteTest(testAtomTypeFromString);

	ExecuteTest(testLetter);
	ExecuteTest(testFormatFloat);

	CleanupMemory();

	return TestSummary();
}
