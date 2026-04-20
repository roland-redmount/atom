
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/letter.h"
#include "kernel/string.h"

#include "testing/testing.h"


void testString(void)
{
	char const * cString1 = "foobar";
	Atom string1 = CreateStringFromCString(cString1);
	ASSERT_UINT32_EQUAL(CStringLength(cString1), ListLength(string1))

	// TODO: reference handling is now different: creating a string
	// includes creating a "type predicate" fact (string @string)
	// which is not part of the defining fact. 
	ASSERT_UINT32_EQUAL(IFactReferenceCount(string1), 1)

	// test reference handling
	IFactAcquire(string1);
	ASSERT_UINT32_EQUAL(IFactReferenceCount(string1), 2)
	IFactRelease(string1);
	ASSERT_UINT32_EQUAL(IFactReferenceCount(string1), 1)

	// second call to CreateString() should return the same string
	// and add one reference
	Atom string1Clone = CreateStringFromCString(cString1);
	ASSERT_DATA64_EQUAL(string1, string1Clone)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(string1), 2)
	IFactRelease(string1Clone);
	ASSERT_UINT32_EQUAL(IFactReferenceCount(string1), 1)

	// create a second string
	char const * cString2 = "fubar";
	Atom string2 = CreateStringFromCString(cString2);
	ASSERT_UINT32_EQUAL(ListLength(string2), CStringLength(cString2))

	// we should have "foobar" < "fubar"
	ASSERT_INT64_EQUAL(ListLexicalOrdering(string1, string2, &CompareTypedAtoms), -1)
	ASSERT_INT64_EQUAL(ListLexicalOrdering(string2, string1, &CompareTypedAtoms), 1)

	IFactRelease(string1);
	IFactRelease(string2);
}


void fuzzTestString(void)
{
	char const * cString = "foobar";

	Atom string = CreateStringFromCString(cString);
	for(index32 i = 0; i < 100; i++) {
		Atom stringClone = CreateStringFromCString(cString);
		ASSERT_DATA64_EQUAL(string, stringClone);
		IFactRelease(stringClone);
	}
	IFactRelease(string);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testString);
	ExecuteTest(fuzzTestString);
	
	KernelShutdown();

	TestSummary();
}

