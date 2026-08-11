
#include "kernel/float.h"
#include "kernel/Int.h"
#include "kernel/UInt.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "lang/Variable.h"
#include "testing/testing.h"


void testGetAtomTypeName(void)
{
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_UINT), "UINT")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_INT), "INT")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_FLOAT), "FLOAT")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_LETTER), "LETTER")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_VARIABLE), "VARIABLE")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_NAME), "NAME")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_ID), "ID")
}


void testAtomTypeFromString(void)
{
	ASSERT_UINT32_EQUAL(AtomTypeFromString("UINT", 4), AT_UINT)
	// the last type name is as reachable as the first
	ASSERT_UINT32_EQUAL(AtomTypeFromString("NAME", 4), AT_NAME)
	ASSERT_UINT32_EQUAL(AtomTypeFromString("PARAMETER", 9), AT_PARAMETER)
	// an unknown name is no type
	ASSERT_UINT32_EQUAL(AtomTypeFromString("FROB", 4), 0)
}


void testVariable(void)
{
	Atom var1 = CreateVariable('X');
	ASSERT_CHAR_EQUAL(GetVariableName(var1), 'x')

	// variables are always lowercase
	Atom var2 = CreateVariable('y');
	ASSERT_CHAR_EQUAL(GetVariableName(var2), 'y')

	Atom var3 = anonymousVariable.atom;
	ASSERT_CHAR_EQUAL(GetVariableName(var3), '_')

	// test quoting
	ASSERT_FALSE(VariableIsQuoted(var1))
	Atom quotedVar1 = QuoteVariable(var1);
	ASSERT_TRUE(VariableIsQuoted(quotedVar1))
	ASSERT_DATA64_EQUAL(UnquoteVariable(quotedVar1).hash, var1.hash)
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
	ExecuteTest(testVariable);

	CleanupMemory();

	TestSummary();
}
