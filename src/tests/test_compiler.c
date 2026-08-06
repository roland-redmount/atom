
#include "kernel/compiler.h"
#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/list.h"
#include "kernel/RelationRegistry.h"
#include "kernel/tuple.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/math.h"
#include "parser/ClauseBuilder.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


// Upper bound on the number of services a single query may compile to
#define MAX_COMPILED_SERVICES	4


void testCompilePermute1(void)
{
	// This rule compiles to a PERMUTE service with no constants
	// + z - x = y  <-  + x + y = z
	DictionaryEntry entry = DictionaryAddClauseFromCString("+ _z - _x = _y | ! + _x + _y = _z");
	Formula * queryTerm = CStringToTerm("+ 7 - 4 = _d");

	// This will yield a new service from the existing (+ + =) service
	ServiceRecord records[MAX_COMPILED_SERVICES];
	size8 nRecords = CompileService(queryTerm, records, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nRecords, 1)
	ServiceRecord record = records[0];

	// TODO: verify the compiled service atom types are correct

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 3);
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	Atom d = TermGetRoleActor(queryTerm->form, arguments, "=", 1);
	ASSERT_UINT64_EQUAL(d._uint, 3);

	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);

	ServiceRegistryRemove(record.relation, record.service);
	RelationRegistryRemove(record.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompilePermute2(void)
{
	// This rule compiles to a PERMUTE service with a constant 2
	// number x addtwo y <- + x + 2 = y
	DictionaryEntry entry = DictionaryAddClauseFromCString("number _x addtwo _y | ! + _x + 2 = _y");
	Formula * queryTerm = CStringToTerm("number 3 addtwo _z");

	ServiceRecord records[MAX_COMPILED_SERVICES];
	size8 nRecords = CompileService(queryTerm, records, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nRecords, 1)
	ServiceRecord record = records[0];

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 3);
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	Atom x = TermGetRoleActor(queryTerm->form, arguments, "number", 1);
	ASSERT_UINT64_EQUAL(x._uint, 3);

	Atom y = TermGetRoleActor(queryTerm->form, arguments, "addtwo", 1);
	ASSERT_UINT64_EQUAL(y._uint, 5);

	// Second call should fail (no more tuples)
	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);

	ServiceRegistryRemove(record.relation, record.service);
	RelationRegistryRemove(record.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry);
}


void testCompilePermute3(void)
{
	// This rule compiles to a PERMUTE service with a variable,
	// which requires wrapping in a DEDUPLICATE service.
	// set s element e <- list s position _ element e
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"set _s element _e | ! list _s position _p element _e");
	Formula * queryTerm = CStringToTerm("set \"alibaba\" element _e");

	// The element role is an untyped output, so the term matches every
	// (list position element) relation: one per element type. We therefore
	// get one compiled service per element type, and must enumerate them all.
	// Only the LETTER-element service yields tuples, as "alibaba" is a string;
	// the ID-element service is registered but matches nothing.
	ServiceRecord records[MAX_COMPILED_SERVICES];
	size8 nRecords = CompileService(queryTerm, records, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nRecords, 2)
	for(index8 i = 0; i < nRecords; i++) {
		PrintServiceRecord(&records[i]);
		PrintChar('\n');
	}

	// The unique letters of "alibaba"
	char uniqueLetters[4] = "abil";
	index8 elementRoleIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(queryTerm->form),
		CreateNameFromCString("element")
	);
	int k = 0;
	for(index8 i = 0; i < nRecords; i++) {
		ASSERT_NOT_NULL(records[i].relation)
		ASSERT_NOT_NULL(records[i].service)

		Atom arguments[2];
		TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
		void * context = ServiceCreateContext(records[i].service, arguments);
		while(ServiceCall(context)) {
			char c = LetterToChar(arguments[elementRoleIndex], LETTER_LOWERCASE);
			ASSERT(k < 4)
			ASSERT_CHAR_EQUAL(c, uniqueLetters[k])
			k++;
		}
		ServiceFreeContext(context);
	}
	ASSERT_UINT32_EQUAL(k, 4);

	for(index8 i = 0; i < nRecords; i++) {
		ServiceRegistryRemove(records[i].relation, records[i].service);
		RelationRegistryRemove(records[i].relation);
	}
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

	ServiceRecord records[MAX_COMPILED_SERVICES];
	size8 nRecords = CompileService(queryTerm, records, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nRecords, 1)
	ServiceRecord record = records[0];

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 3);
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	Atom y = TermGetRoleActor(queryTerm->form, arguments, "second", 1);
	ASSERT_UINT64_EQUAL(y._uint, 4);

	Atom z = TermGetRoleActor(queryTerm->form, arguments, "third", 1);
	ASSERT_UINT64_EQUAL(z._uint, 5);

	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);

	ServiceRegistryRemove(record.relation, record.service);
	RelationRegistryRemove(record.relation);
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

	ServiceRecord records[MAX_COMPILED_SERVICES];
	size8 nRecords = CompileService(queryTerm, records, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nRecords, 1)
	ServiceRecord record = records[0];
	// PrintCString("Service  = ");
	// PrintService(record.service);
	// PrintChar('\n');

	// Call the service
	Atom arguments[2];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 2);
	void * context = ServiceCreateContext(record.service, arguments);
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

	ServiceRegistryRemove(record.relation, record.service);
	RelationRegistryRemove(record.relation);
	FreeFormula(queryTerm);
	DictionaryRemoveClause(&entry1);
	DictionaryRemoveClause(&entry2);
}


void testCompileRecursiveJoin(void)
{
	// TODO: Compile a recursive rule to a JOIN service
	// number n faculty f <- + m + 1 = n & number m faculty e & * e * n = f
	DictionaryEntry entry = DictionaryAddClauseFromCString(
		"number _n faculty _f | ! + _m + 1 = _n | ! number _m faculty _e | ! * _e * _n = _f");
	Formula * queryTerm = CStringToTerm("number 4 faculty _f");

	ServiceRecord records[MAX_COMPILED_SERVICES];
	size8 nRecords = CompileService(queryTerm, records, MAX_COMPILED_SERVICES);
	ASSERT_UINT32_EQUAL(nRecords, 1)
	ServiceRecord record = records[0];

	// Call the service
	Atom arguments[3];
	TupleCopy(TypedTuplePeekAtoms(queryTerm->actors), arguments, 3);
	void * context = ServiceCreateContext(record.service, arguments);
	ASSERT_TRUE(ServiceCall(context))

	Atom f = TermGetRoleActor(queryTerm->form, arguments, "faculty", 1);
	ASSERT_UINT64_EQUAL(f._uint, 24);

	ASSERT_FALSE(ServiceCall(context))
	ServiceFreeContext(context);

	ServiceRegistryRemove(record.relation, record.service);
	RelationRegistryRemove(record.relation);
	FreeFormula(queryTerm);
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
	// ExecuteTest(testCompileRecursiveJoin);

	MathTeardown();
	TestSummary();
}
