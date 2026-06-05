
#include "kernel/compiler.h"
#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "kernel/list.h"
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
	PrintCString("Service record: ");
	PrintServiceRecord(&record);
	PrintChar('\n');

	// TODO: call the compiled service

	// Argument list
	Tuple * arguments = CreateTuple(3);
	CopyListToTuple(FormulaGetActors(queryTerm), arguments);
	
	void * context = ServiceCreateContext(&record.service, arguments);

	// Call the expression
	// this should yields 3 elements corresponding to the 3 roles of (list position element)
	size32 nElements = 0;
	while(ServiceCall(context)) {
		PrintTuple(arguments);
		PrintChar('\n');
		nElements++;
	}
	ServiceFreeContext(context);
	FreeTuple(arguments);

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
