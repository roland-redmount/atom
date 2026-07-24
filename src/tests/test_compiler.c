
#include "kernel/compiler.h"
#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "kernel/list.h"
#include "kernel/tuple.h"
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
	Formula * queryTerm = CStringToTerm("+ 7 - 4 = _d");

	// This will yield a new service from the existing (+ + =) service
	Service const * service = CompileService(queryTerm);
	ASSERT_NOT_NULL(service)

	// TODO: verify the compiled service atom types are correct

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 3);
	void * context = ServiceCreateContext(service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	Atom d = TermGetRoleActor(queryTerm->form, arguments, "=", 1);
	ASSERT_UINT64_EQUAL(d._uint, 3);

	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);

	// RegistryRemoveService(&record);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompilePermute2(void)
{
	// This rule compiles to a PERMUTE service with a constant 2
	// number x addtwo y <- + x + 2 = y
	DictionaryEntry entry = DictionaryAddClauseFromCString("number _x addtwo _y | ! + _x + 2 = _y");
	Formula * queryTerm = CStringToTerm("number 3 addtwo _z");

	Service const * service = CompileService(queryTerm);
	ASSERT_NOT_NULL(service)

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 3);
	void * context = ServiceCreateContext(service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	Atom x = TermGetRoleActor(queryTerm->form, arguments, "number", 1);
	ASSERT_UINT64_EQUAL(x._uint, 3);

	Atom y = TermGetRoleActor(queryTerm->form, arguments, "addtwo", 1);
	ASSERT_UINT64_EQUAL(y._uint, 5);

	// Second call should fail (no more tuples)
	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);

	// RegistryRemoveService(&record);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompilePermute3(void)
{
	// This rule compiles to a PERMUTE service with a variable,
	// which requires wrapping in a DEDUPLICATE service.
	// set s element e <- list s position _ element e
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"set _s element _e | ! list _s position _ element _e");
	Formula * queryTerm = CStringToTerm("set \"alibaba\" element _e");

	Service const * service = CompileService(queryTerm);
	ASSERT_NOT_NULL(service)

	// Call the service
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
	void * context = ServiceCreateContext(service, arguments);
	size8 nElements = 0;
	while(ServiceCall(context)) {
		nElements++;
	}
	ASSERT_UINT32_EQUAL(nElements, 4);
	ServiceFreeContext(context);

	// RegistryRemoveService(&record);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompileJoin1(void)
{
	// This rule compiles to a JOIN service
	// first x second y third z  <-  + x + 1 = y & + y + 1 = z
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"first _x second _y third _z | ! + _x + 1 = _y | ! + _y + 1 = _z");
	Formula * queryTerm = CStringToTerm("first 3 second _s third _t");

	Service const * service = CompileService(queryTerm);
	ASSERT_NOT_NULL(service)

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 3);
	void * context = ServiceCreateContext(service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	Atom y = TermGetRoleActor(queryTerm->form, arguments, "second", 1);
	ASSERT_UINT64_EQUAL(y._uint, 4);

	Atom z = TermGetRoleActor(queryTerm->form, arguments, "third", 1);
	ASSERT_UINT64_EQUAL(z._uint, 5);

	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);

	// RegistryRemoveService(&record);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompileUnion(void)
{
	// Two rules resulting in a UNION service
	// number x neighbor y <- = y + x + 1     (y = x + 1)
	// number x neighbor y <- = x + y + 1     (x = y - 1 <-> y = x - 1)
	DictionaryEntry entry1 = DictionaryAddClauseFromCString(
		"number _x neighbor _y | ! = _y + _x + 1");
	DictionaryEntry entry2 = DictionaryAddClauseFromCString(
		"number _x neighbor _y | ! = _x + _y + 1");
	Formula * queryTerm = CStringToTerm("number 5 neighbor _y");

	Service const * service = CompileService(queryTerm);
	ASSERT_NOT_NULL(service)
	PrintCString("Service  = ");
	PrintService(service);
	PrintChar('\n');

	// Call the service
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
	void * context = ServiceCreateContext(service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	// The atom types are encoded in the relation table associated with
	// the compiled service ...
	// PrintTuple(atomTypes?, arguments, 3);
	// PrintChar('\n');
	Atom y = TermGetRoleActor(queryTerm->form, arguments, "neighbor", 1);
	ASSERT_TRUE(y._uint == 4);

	ASSERT_TRUE(ServiceCall(context))
	// PrintTuple(arguments, 3);
	// PrintChar('\n');
	y = TermGetRoleActor(queryTerm->form, arguments, "neighbor", 1);
	ASSERT_TRUE(y._uint == 6);

	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);

	// RegistryRemoveService(&record);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry1);
	DictionaryRemoveClause(&entry2);
}


void testCompileRecursiveJoin(void)
{
	// TODO: Compile a recursive rule to a JOIN service
	// number n faculty f <- + m + 1 = n & number m faculty e & * e * n = f
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"number _n faculty _f | ! + _m + 1 = _n | ! number _m faculty _e | * _e * _n = _f");
	Formula * queryTerm = CStringToTerm("number 4 faculty _f");

	Service const * service = CompileService(queryTerm);
	ASSERT_NOT_NULL(service)

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 3);
	void * context = ServiceCreateContext(service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	Atom f = TermGetRoleActor(queryTerm->form, arguments, "faculty", 1);
	ASSERT_UINT64_EQUAL(f._uint, 24);

	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);

	// RegistryRemoveService(&record);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();

	ExecuteTest(testCompilePermute1);
	ExecuteTest(testCompilePermute2);
	// TODO: this one requires migrating to typed relation tables
	// ExecuteTest(testCompilePermute3);
	ExecuteTest(testCompileJoin1);
	ExecuteTest(testCompileUnion);

	MathTeardown();
	TestSummary();
}
