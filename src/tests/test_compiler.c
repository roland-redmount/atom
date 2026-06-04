
#include "kernel/compiler.h"
#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "lang/Formula.h"
#include "library/math.h"
#include "parser/ClauseBuilder.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


void testCompile1(void)
{
	// Rule:  + z - x = y  <-  + x + y = z
	Atom rule = CStringToClause("+ _z - _x = _y | ! + _x + _y = _z");
	DictionaryAddClause(rule);
	
	Atom queryTerm = CStringToTerm("+ 7 - 4 = _d");
	PrintCString("queryTerm = ");
	PrintFormula(queryTerm);
	PrintChar('\n');

	// This will yield a new service from the existing (+ + =) service
	ServiceRecord record;
	ASSERT_TRUE(CompileService(queryTerm, &record))

	// TODO: call the compiled service

	RegistryRemoveService(&record);
	IFactRelease(queryTerm);
	DictionaryRemoveClause(rule);
	IFactRelease(rule);
}


void testCompile2(void)
{
	// Rule: first x second y third z  <-  + x + 1 = y & + y + 1 = z
	Atom rule = CStringToClause(
		"first x second y third z | ! + x + 1 = y | ! + y + 1 = z");
	DictionaryAddClause(rule);
	
	Atom queryTerm = CStringToTerm("first 3 second s third t");
	PrintCString("queryTerm = ");
	PrintFormula(queryTerm);
	PrintChar('\n');

	// This will yield a JOIN expression
	ServiceRecord record;
	ASSERT_TRUE(CompileService(queryTerm, &record))

	IFactRelease(queryTerm);
	DictionaryRemoveClause(rule);
	IFactRelease(rule);
}



int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testCompile1);

	MathTeardown();
	TestSummary();
}
