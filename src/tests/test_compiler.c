
#include "kernel/compiler.h"
#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "lang/Formula.h"
#include "library/math.h"
#include "parser/ClauseBuilder.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


void testCompile(void)
{
	// The rule  + x + y = z  ->  + z - x = y
	Atom rule = CStringToClause(" ! + _x + _y = _z | + _z - _x = _y");
	DictionaryAddClause(rule);
	
	Atom queryTerm = CStringToTerm("+ 7 - 4 = _d");
	PrintCString("queryTerm = ");
	PrintFormula(queryTerm);
	PrintChar('\n');

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

	ExecuteTest(testCompile);

	MathTeardown();
	TestSummary();
}
