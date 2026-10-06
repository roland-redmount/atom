
#include "kernel/dictionary.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "lang/ClauseForm.h"
#include "lang/formula.h"
#include "library/library.h"
#include "library/string.h"
#include "parser/ClauseBuilder.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


void testDictionary(void)
{
	size8 const arity = 5;
	Atom rule = CStringToClause("!number x square s | * x * x = s");

	FormulaView clause = DictionaryAddClause(rule);

	// test iteration
	DictionaryIterator iterator;
	DictionaryIterateClauses(FormulaGetForm(rule), &iterator);
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
	DictionaryRemoveClause(&clause);
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

	FormulaView clause = DictionaryAddClause(rule);
	ASSERT_TRUE(DictionaryContainsClause(rule))

	FormulaView sameClause = DictionaryAddClause(rule);
	ASSERT_PTR_EQUAL(sameClause.actors, clause.actors)
	ASSERT_DATA64_EQUAL(sameClause.form.hash, clause.form.hash)

	// the clause is in the dictionary once, so one removal takes it
	DictionaryIterator iterator;
	DictionaryIterateClauses(FormulaGetForm(rule), &iterator);
	ASSERT_TRUE(DictionaryIteratorNext(&iterator))
	ASSERT_FALSE(DictionaryIteratorNext(&iterator))
	DictionaryIteratorEnd(&iterator);

	DictionaryRemoveClause(&clause);
	ASSERT_FALSE(DictionaryContainsClause(rule))
	ReleaseFormula(rule);
}


/**
 * Test adding ifact rules to the dictionary. Two ifact rules are equal
 * if they have the same term form and generator role, regardless of variable names.
 */
void testIFactRules(void)
{
	Atom rule = CStringToTerm("circle * radius r");
	Atom sameRule = CStringToTerm("circle * radius s");
	Atom termForm = FormulaGetForm(rule);
	ASSERT_TRUE(IsIFactRule(rule))
	ASSERT_FALSE(DictionaryContainsIFactRule(rule))
	ASSERT_FALSE(IFactRuleExistsForTermForm(termForm))

	FormulaView ifactRule = DictionaryAddIFactRule(rule);
	ASSERT_TRUE(DictionaryContainsIFactRule(rule))
	ASSERT_TRUE(DictionaryContainsIFactRule(sameRule))
	ASSERT_TRUE(IFactRuleExistsForTermForm(termForm))

	// Adding an equal rule yields the entry already there
	FormulaView sameIFactRule = DictionaryAddIFactRule(sameRule);
	ASSERT_PTR_EQUAL(sameIFactRule.actors, ifactRule.actors)

	size32 nRules = 0;
	DictionaryIterator iterator;
	DictionaryIterateIFactRules(termForm, &iterator);
	while(DictionaryIteratorNext(&iterator))
		nRules++;
	DictionaryIteratorEnd(&iterator);
	ASSERT_UINT32_EQUAL(nRules, 1)

	DictionaryRemoveIFactRule(&ifactRule);
	ASSERT_FALSE(DictionaryContainsIFactRule(rule))
	ASSERT_FALSE(IFactRuleExistsForTermForm(termForm))
	ReleaseFormula(sameRule);
	ReleaseFormula(rule);
}


/**
 * Test formulas that are not valid ifact rules; see IsIFactRule().
 */
void testIFactRuleRejects(void)
{
	char const * const terms[] = {
		"circle * radius 5.0",			// a constant
		"circle * radius *",			// two generators
		"circle x radius r",			// no generator
		"! circle * radius r",			// a negated term
		"rect * width x height x",		// a repeated variable
	};
	for(index8 i = 0; i < sizeof(terms) / sizeof(terms[0]); i++) {
		Atom term = CStringToTerm(terms[i]);
		ASSERT_FALSE(IsIFactRule(term))
		ReleaseFormula(term);
	}

	Atom clause = CStringToClause("circle * radius r | ! square s side r");
	ASSERT_FALSE(IsIFactRule(clause))
	ReleaseFormula(clause);
}


int main(int argc, char * argv[])
{
	KernelInitialize(TRANSIENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testDictionary);
	ExecuteTest(testDictionaryAddTwice);
	ExecuteTest(testIFactRules);
	ExecuteTest(testIFactRuleRejects);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}

