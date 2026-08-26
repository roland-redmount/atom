
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
		Atom letter = GetAlphabetLetter(c);
		ASSERT_DATA64_EQUAL(letter.letter.code, i)
		i++;
	}

	i = 1;
	for(char c = 'a'; c <= 'z'; c++) {
		Atom letter = GetAlphabetLetter(c);
		ASSERT_DATA64_EQUAL(letter.letter.code, i)
		i++;
	}
}


int main(int argc, char * argv[])
{
	SetupMemory();

	ExecuteTest(testGetAtomTypeName);
	ExecuteTest(testAtomTypeFromString);

	ExecuteTest(testLetter);

	CleanupMemory();

	TestSummary();
}
