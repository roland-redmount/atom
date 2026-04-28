
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

	DictionaryAddClause(rule);

	DictionaryIterator iterator;
	DictionaryIterate(FormulaGetForm(rule), &iterator);
	ASSERT_TRUE(DictionaryIteratorHasRecord(&iterator))
	// test that actors tuple is identical to the formula
	Tuple const * actorsTuple = DictionaryIteratorPeekActors(&iterator);
	for(index8 i = 0; i < arity; i++) {
		ASSERT_TRUE(
			SameTypedAtoms(
				TupleGetElement(actorsTuple, i),
				ListGetElement(FormulaGetActors(rule), i + 1)
			)
		)
	}
	DictionaryIteratorNext(&iterator);
	ASSERT_FALSE(DictionaryIteratorHasRecord(&iterator))
	DictionaryIteratorEnd(&iterator);

	DictionaryRemoveClause(rule);

	IFactRelease(rule);
}


void testDictionaryIterator(void)
{
	// create a form

	// iterate over matching rules
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testDictionary);

	KernelShutdown();

	TestSummary();
}

