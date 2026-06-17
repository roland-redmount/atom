
#include "kernel/dictionary.h"
#include "kernel/ifact.h"
#include "kernel/list.h"
#include "kernel/kernel.h"
#include "kernel/string.h"
#include "lang/ClauseForm.h"
#include "lang/Formula.h"
#include "parser/ClauseBuilder.h"
#include "testing/testing.h"


void testDictionary(void)
{
	size8 const arity = 5;
	Atom rule = CStringToClause("!number _x square _s | * _x * _x = _s");

	DictionaryEntry entry = DictionaryAddClause(rule);

	// test iteration
	DictionaryIterator iterator;
	DictionaryIterate(FormulaGetForm(rule), &iterator);
	ASSERT_TRUE(DictionaryIteratorNext(&iterator))
	// test that actors tuple is identical to the formula
	TypedTuple const * actorsTuple = DictionaryIteratorPeekActors(&iterator);
	for(index8 i = 0; i < arity; i++) {
		ASSERT_TRUE(
			SameTypedAtoms(
				TypedTupleGetElement(actorsTuple, i),
				ListGetElement(FormulaGetActors(rule), i + 1)
			)
		)
	}
	ASSERT_FALSE(DictionaryIteratorNext(&iterator))
	DictionaryIteratorEnd(&iterator);

	// test remove tuple
	DictionaryRemoveClause(&entry);
	IFactRelease(rule);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testDictionary);

	KernelShutdown();

	TestSummary();
}

