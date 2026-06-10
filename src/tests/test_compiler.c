
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


void testCompilePermute1(void)
{
	// This rule compiles to a PERMUTE service with a constant 2
	// number x addtwo y <- + x + 2 = y
	Atom rule = CStringToClause("number _x addtwo _y | ! + _x + 2 = _y");
	DictionaryAddClause(rule);
	
	Atom queryTerm = CStringToTerm("number 3 addtwo _z");
	PrintCString("queryTerm = ");
	PrintFormula(queryTerm);
	PrintChar('\n');

	ServiceRecord record;
	ASSERT_TRUE(CompileService(queryTerm, &record))
	PrintCString("Service record: ");
	PrintServiceRecord(&record);
	PrintChar('\n');
/*
	// Call the service
	// this should yields 3 elements corresponding to the 3 roles of (list position element)
	Tuple * arguments = CreateTuple(2);
	CopyListToTuple(FormulaGetActors(queryTerm), arguments);
	void * context = ServiceCreateContext(record.service, arguments);
	size32 nElements = 0;
	while(ServiceCall(context)) {
		PrintTuple(arguments);
		PrintChar('\n');
		nElements++;
	}
	ServiceFreeContext(context);
	FreeTuple(arguments);
*/
	RegistryRemoveService(&record);
	IFactRelease(queryTerm);
	DictionaryRemoveClause(rule);
	IFactRelease(rule);
}


void testCompilePermute2(void)
{
	// This rule compiles to a PERMUTE service with no constants
	// + z - x = y  <-  + x + y = z
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

	// Call the service
	// this should yields 3 elements corresponding to the 3 roles of (list position element)
	Tuple * arguments = CreateTuple(3);
	CopyListToTuple(FormulaGetActors(queryTerm), arguments);
	void * context = ServiceCreateContext(record.service, arguments);
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


void testCompileJoin1(void)
{
	// This rule compiles to a JOIN service
	// first x second y third z  <-  + x + 1 = y & + y + 1 = z
	Atom rule = CStringToClause(
		"first _x second _y third _z | ! + _x + 1 = _y | ! + _y + 1 = _z");
	DictionaryAddClause(rule);
	
	Atom queryTerm = CStringToTerm("first 3 second _s third _t");
	PrintCString("queryTerm = ");
	PrintFormula(queryTerm);
	PrintChar('\n');

	ServiceRecord record;
	ASSERT_TRUE(CompileService(queryTerm, &record))

	// Call the service
	Tuple * arguments = CreateTuple(3);
	CopyListToTuple(FormulaGetActors(queryTerm), arguments);
	void * context = ServiceCreateContext(record.service, arguments);
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


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testCompilePermute1);
	ExecuteTest(testCompilePermute2);
	ExecuteTest(testCompileJoin1);

	MathTeardown();
	TestSummary();
}
