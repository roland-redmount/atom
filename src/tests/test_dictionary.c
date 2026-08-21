
#include "kernel/dictionary.h"
#include "kernel/ifact.h"
#include "kernel/list.h"
#include "kernel/kernel.h"
#include "kernel/string.h"
#include "lang/ClauseForm.h"
#include "lang/formula.h"
#include "parser/ClauseBuilder.h"
#include "testing/testing.h"


void testDictionary(void)
{
	size8 const arity = 5;
	Atom rule = CStringToClause("!number x square s | * x * x = s");

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
				TypedTupleGetElement(FormulaGetActors(rule), i)
			)
		)
	}
	ASSERT_FALSE(DictionaryIteratorNext(&iterator))
	DictionaryIteratorEnd(&iterator);

	// test remove tuple
	DictionaryRemoveClause(&entry);
	ReleaseFormula(rule);
}


/**
 * A clause already in the dictionary is not added a second time: the entry already there
 * is yielded, and one entry is all there is to iterate and to remove.
 */
void testDictionaryAddTwice(void)
{
	Atom rule = CStringToClause("before x after y | ! prec x succ y");
	ASSERT_FALSE(DictionaryContainsClause(rule))

	DictionaryEntry entry = DictionaryAddClause(rule);
	ASSERT_TRUE(DictionaryContainsClause(rule))

	DictionaryEntry sameEntry = DictionaryAddClause(rule);
	ASSERT_PTR_EQUAL(sameEntry.tuple, entry.tuple)
	ASSERT_DATA64_EQUAL(sameEntry.clauseForm.hash, entry.clauseForm.hash)

	// the clause is in the dictionary once, so one removal takes it
	DictionaryIterator iterator;
	DictionaryIterate(FormulaGetForm(rule), &iterator);
	ASSERT_TRUE(DictionaryIteratorNext(&iterator))
	ASSERT_FALSE(DictionaryIteratorNext(&iterator))
	DictionaryIteratorEnd(&iterator);

	DictionaryRemoveClause(&entry);
	ASSERT_FALSE(DictionaryContainsClause(rule))
	ReleaseFormula(rule);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testDictionary);
	ExecuteTest(testDictionaryAddTwice);

	KernelShutdown();

	TestSummary();
}

