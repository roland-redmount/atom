
#include "kernel/FloatIEEE754.h"
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
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_FLOAT32), "FLOAT32")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_FLOAT64), "FLOAT64")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_LETTER), "LETTER")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_VARIABLE), "VARIABLE")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_NAME), "NAME")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_ID), "ID")
	ASSERT_STRING_EQUAL(GetAtomTypeName(AT_INSTRUCTION), "INSTRUCTION")
}


void testAtomTypeFromString(void)
{
	ASSERT_UINT32_EQUAL(AtomTypeFromString("UINT", 4), AT_UINT)
}


void testFloat(void)
{
	double float64Value = 3.1415926535897932384;
	TypedAtom float64 = CreateFloat64(float64Value);
	ASSERT_DOUBLE_EQUAL(GetFloat64Value(float64), float64Value)

	float float32Value = 3.141592;
	TypedAtom float32 = CreateFloat32((float) float32Value);
	ASSERT_FLOAT_EQUAL(GetFloat32Value(float32), float32Value)
}


void testInt(void)
{
	const int values[] = {42, 0, -7};
	size32 n_values = sizeof(values) / sizeof(int);
	for(index32 i = 0; i < n_values; i++) {
		TypedAtom integer = CreateTypedAtom(AT_INT, values[i]);
		ASSERT_INT64_EQUAL(integer.atom, values[i])
	}
}


void testUInt(void)
{
	const uint64 values[] = {42, 0, 0xFFFFFFFFFFFFFFFFUL};
	size32 n_values = sizeof(values) / sizeof(uint64);
	for(index32 i = 0; i < n_values; i++) {
		TypedAtom integer = CreateTypedAtom(AT_UINT, values[i]);
		ASSERT_UINT64_EQUAL(integer.atom, values[i])
	}
}


void testVariable(void)
{
	TypedAtom var1 = CreateVariable('X');
	ASSERT_CHAR_EQUAL(GetVariableName(var1), 'x')

	// variables are always lowercase
	TypedAtom var2 = CreateVariable('y');
	ASSERT_CHAR_EQUAL(GetVariableName(var2), 'y')

	TypedAtom var3 = anonymousVariable;
	ASSERT_CHAR_EQUAL(GetVariableName(var3), '_')

	// test quoting
	ASSERT_FALSE(VariableIsQuoted(var1))
	TypedAtom quotedVar1 = QuoteVariable(var1);
	ASSERT_TRUE(VariableIsQuoted(quotedVar1))
	ASSERT_TRUE(SameTypedAtoms(UnquoteVariable(quotedVar1), var1))
}


static void testLetter(void)
{
	index8 i = 1;
	for(char c = 'A'; c <= 'Z'; c++) {
		TypedAtom letter = GetAlphabetLetter(c);
		ASSERT_UINT32_EQUAL(letter.type, AT_LETTER)
		ASSERT_DATA64_EQUAL(letter.atom, i)
		i++;
	}

	i = 1;
	for(char c = 'a'; c <= 'z'; c++) {
		TypedAtom letter = GetAlphabetLetter(c);
		ASSERT_UINT32_EQUAL(letter.type, AT_LETTER)
		ASSERT_DATA64_EQUAL(letter.atom, i)
		i++;
	}
}


int main(int argc, char * argv[])
{
	SetupMemory();

	ExecuteTest(testGetAtomTypeName);
	ExecuteTest(testAtomTypeFromString);

	ExecuteTest(testInt);
	ExecuteTest(testUInt);
	ExecuteTest(testFloat);
	ExecuteTest(testLetter);
	ExecuteTest(testVariable);

	CleanupMemory();

	TestSummary();
}
