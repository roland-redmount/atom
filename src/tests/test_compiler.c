
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
	// This rule compiles to a PERMUTE service with no constants
	// + z - x = y  <-  + x + y = z
	DictionaryEntry entry = DictionaryAddClauseFromCString("+ _z - _x = _y | ! + _x + _y = _z");
	
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
	DictionaryRemoveClause(&entry);
}


void testCompilePermute2(void)
{
	// This rule compiles to a PERMUTE service with a constant 2
	// number x addtwo y <- + x + 2 = y
	DictionaryEntry entry = DictionaryAddClauseFromCString("number _x addtwo _y | ! + _x + 2 = _y");
	
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
	DictionaryRemoveClause(&entry);
}


void testCompilePermute3(void)
{
	// This rule compiles to a PERMUTE service with a variable,
	// which requires wrapping in a DEDUPLICATE service.
	// set s element e <- list s position _ element e
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"set _s element _e | ! list _s position _ element _e");
	
	Atom queryTerm = CStringToTerm("set \"alibaba\" element _e");
	PrintCString("queryTerm = ");
	PrintFormula(queryTerm);
	PrintChar('\n');

	ServiceRecord record;
	ASSERT_TRUE(CompileService(queryTerm, &record))
	PrintCString("Service record: ");
	PrintServiceRecord(&record);
	PrintChar('\n');

	// Call the service
	Tuple * arguments = CreateTuple(2);
	CopyListToTuple(FormulaGetActors(queryTerm), arguments);
	void * context = ServiceCreateContext(record.service, arguments);
	size8 nElements = 0;
	while(ServiceCall(context)) {
		nElements++;
	}
	ASSERT_UINT32_EQUAL(nElements, 4);
	ServiceFreeContext(context);
	FreeTuple(arguments);

	RegistryRemoveService(&record);
	IFactRelease(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompileJoin1(void)
{
	// This rule compiles to a JOIN service
	// first x second y third z  <-  + x + 1 = y & + y + 1 = z
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"first _x second _y third _z | ! + _x + 1 = _y | ! + _y + 1 = _z");

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
	DictionaryRemoveClause(&entry);
}


void testCompileUnion(void)
{
	// Two rules resulting in a UNION service
	// number x neighbor y <- + x + 1 = y
	// number x neighbor y <- + x - 1 = y
	DictionaryEntry entry1 = DictionaryAddClauseFromCString(
		"number _x neighbor _y | ! + _x + 1 = _y");
	DictionaryEntry entry2 = DictionaryAddClauseFromCString(
		"number _x neighbor _y | ! + _y + 1 = _x");

	Atom queryTerm = CStringToTerm("number 5 neighbor _y");
	ServiceRecord record;
	ASSERT_TRUE(CompileService(queryTerm, &record))

	// Call the service
	Tuple * arguments = CreateTuple(2);
	CopyListToTuple(FormulaGetActors(queryTerm), arguments);
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	TypedAtom y = TermGetRoleActor(record.form, arguments, "neighbor", 1);
	ASSERT_UINT32_EQUAL(y.type, AT_INT)
	ASSERT_TRUE(y.atom == 4);

	y = TermGetRoleActor(record.form, arguments, "neighbor", 1);
	ASSERT_UINT32_EQUAL(y.type, AT_INT)
	ASSERT_TRUE(y.atom == 6);

	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);
	FreeTuple(arguments);

	RegistryRemoveService(&record);
	IFactRelease(queryTerm);
	DictionaryRemoveClause(&entry1);
	DictionaryRemoveClause(&entry2);
}


void testCompileRecursiveJoin(void)
{
	// TODO: Compile a recursive rule to a JOIN service
	// number n faculty f <- + m + 1 = n & number m faculty e & * e * n = f
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"number _n faculty _f | ! + _m + 1 = _n | ! number _m faculty _e | * _e * _n = _f");

	Atom queryTerm = CStringToTerm("number 4 faculty _f");
	ServiceRecord record;
	ASSERT_TRUE(CompileService(queryTerm, &record))

	// Call the service
	Tuple * arguments = CreateTuple(3);
	CopyListToTuple(FormulaGetActors(queryTerm), arguments);
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	TypedAtom f = TermGetRoleActor(record.form, arguments, "faculty", 1);
	ASSERT_UINT32_EQUAL(f.type, AT_INT)
	ASSERT_UINT64_EQUAL(f.atom, 24);

	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);
	FreeTuple(arguments);

	RegistryRemoveService(&record);
	IFactRelease(queryTerm);
	DictionaryRemoveClause(&entry);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testCompilePermute1);
	ExecuteTest(testCompilePermute2);
	ExecuteTest(testCompilePermute3);
	ExecuteTest(testCompileJoin1);
	ExecuteTest(testCompileUnion);

	MathTeardown();
	TestSummary();
}
