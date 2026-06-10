
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
	// PrintCString("queryTerm = ");
	// PrintFormula(queryTerm);
	// PrintChar('\n');

	ServiceRecord record;
	ASSERT_TRUE(CompileService(queryTerm, &record))
	// PrintCString("Service record: ");
	// PrintServiceRecord(&record);
	// PrintChar('\n');

	// Call the service
	Tuple * arguments = CreateTuple(2);
	CopyListToTuple(FormulaGetActors(queryTerm), arguments);
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	TypedAtom x = TermGetRoleActor(record.form, arguments, "number", 1);
	ASSERT_UINT32_EQUAL(x.type, AT_INT)
	ASSERT_UINT64_EQUAL(x.atom, 3);

	TypedAtom y = TermGetRoleActor(record.form, arguments, "addtwo", 1);
	ASSERT_UINT32_EQUAL(y.type, AT_INT)
	ASSERT_UINT64_EQUAL(y.atom, 5);

	// Second call should fail (no more tuples)
	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);
	FreeTuple(arguments);

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
	// PrintCString("queryTerm = ");
	// PrintFormula(queryTerm);
	// PrintChar('\n');

	// This will yield a new service from the existing (+ + =) service
	ServiceRecord record;
	ASSERT_TRUE(CompileService(queryTerm, &record))
	// PrintCString("Service record: ");
	// PrintServiceRecord(&record);
	// PrintChar('\n');

	// Call the service
	Tuple * arguments = CreateTuple(3);
	CopyListToTuple(FormulaGetActors(queryTerm), arguments);
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	TypedAtom d = TermGetRoleActor(record.form, arguments, "=", 1);
	ASSERT_UINT32_EQUAL(d.type, AT_INT)
	ASSERT_UINT64_EQUAL(d.atom, 3);

	ASSERT_FALSE(ServiceCall(context))
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

	ServiceRecord record;
	ASSERT_TRUE(CompileService(queryTerm, &record))

	// Call the service
	Tuple * arguments = CreateTuple(3);
	CopyListToTuple(FormulaGetActors(queryTerm), arguments);
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	TypedAtom y = TermGetRoleActor(record.form, arguments, "second", 1);
	ASSERT_UINT32_EQUAL(y.type, AT_INT)
	ASSERT_UINT64_EQUAL(y.atom, 4);

	TypedAtom z = TermGetRoleActor(record.form, arguments, "third", 1);
	ASSERT_UINT32_EQUAL(z.type, AT_INT)
	ASSERT_UINT64_EQUAL(z.atom, 5);

	ASSERT_FALSE(ServiceCall(context))
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
