/**
 * Tests for the high level fact interface, AssertFact() and RetractFact().
 */

#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "kernel/RelationTableRegistry.h"
#include "lang/formula.h"
#include "parser/ClauseBuilder.h"
#include "parser/ConjunctionBuilder.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"
#include "ui/assert.h"


/**
 * Assert and retract two facts of a relation that does not exist beforehand.
 * The first assert creates the relation and its table, and retracting the last
 * fact drops both again.
 */
void testAssertRetract(void)
{
	// Two facts of the term form (foo bar), with the same atom types,
	// so that both belong to the same relation.
	Atom fact1 = CStringToTerm("foo \"barf\" bar 1");
	Atom fact2 = CStringToTerm("foo \"baz\" bar 42");
	size8 nColumns = FormulaGetActors(fact1)->nAtoms;
	byte const * atomTypes = TypedTuplePeekAtomTypes(FormulaGetActors(fact1));

	// The relation does not exist until the first fact is asserted
	ASSERT_NULL(RelationRegistryFind(FormulaGetForm(fact1), nColumns, atomTypes))

	ASSERT_INT32_EQUAL(AssertFact(FormulaGetForm(fact1), FormulaGetActors(fact1), 0), ASSERT_OK)

	Relation const * relation = RelationRegistryFind(FormulaGetForm(fact1), nColumns, atomTypes);
	ASSERT_NOT_NULL(relation)
	RelationTable const * table = RelationTableRegistryFind(relation);
	ASSERT_NOT_NULL(table)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 1)

	// Asserting the same fact again changes nothing
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetForm(fact1), FormulaGetActors(fact1), 0), ASSERT_EXISTED)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 1)

	// The second fact goes in the same table
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetForm(fact2), FormulaGetActors(fact2), 0), ASSERT_OK)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 2)

	RetractFact(FormulaGetForm(fact2), FormulaGetActors(fact2));
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 1)

	// Retracting the last fact drops the table, and the relation with it
	RetractFact(FormulaGetForm(fact1), FormulaGetActors(fact1));
	ASSERT_NULL(RelationRegistryFind(FormulaGetForm(fact1), nColumns, atomTypes))

	ReleaseFormula(fact2);
	ReleaseFormula(fact1);
}


/**
 * A fact contradicts the knowledge base when its negation is a tuple of a stored
 * relation, and is refused. Asserting the negation of a stored fact is symmetric,
 * so it does not matter which of the two is asserted first.
 */
void testAssertContradictsStoredFact(void)
{
	Atom fact = CStringToTerm("prec \"a\" succ \"b\"");
	Atom negatedFact = CStringToTerm("! prec \"a\" succ \"b\"");
	size8 nColumns = FormulaGetActors(negatedFact)->nAtoms;
	byte const * atomTypes = TypedTuplePeekAtomTypes(FormulaGetActors(negatedFact));

	ASSERT_INT32_EQUAL(AssertFact(FormulaGetForm(fact), FormulaGetActors(fact), 0), ASSERT_OK)

	// (! prec "a" succ "b") is refused, contradicting the fact just asserted
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetForm(negatedFact), FormulaGetActors(negatedFact), 0), ASSERT_FAIL)
	// and the refused assert leaves no relation behind
	ASSERT_NULL(RelationRegistryFind(FormulaGetForm(negatedFact), nColumns, atomTypes))

	// Retracting the fact it contradicts makes the same assert succeed
	RetractFact(FormulaGetForm(fact), FormulaGetActors(fact));
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetForm(negatedFact), FormulaGetActors(negatedFact), 0), ASSERT_OK)

	// and the positive fact is now the one refused
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetForm(fact), FormulaGetActors(fact), 0), ASSERT_FAIL)

	RetractFact(FormulaGetForm(negatedFact), FormulaGetActors(negatedFact));
	ReleaseFormula(negatedFact);
	ReleaseFormula(fact);
}


/**
 * A fact also contradicts the knowledge base when its negation is not stored, but
 * derived by a rule. Finding the contradiction then compiles the query for the
 * negated term; see checkContradiction() in assert.c.
 */
void testAssertContradictsDerivedFact(void)
{
	// (! even x) follows from (odd x), so (odd 3) entails (! even 3)
	DictionaryEntry entry = DictionaryAddClauseFromCString("! even x | ! odd x");
	Atom odd3 = CStringToTerm("odd 3");
	Atom even3 = CStringToTerm("even 3");
	Atom even4 = CStringToTerm("even 4");

	ASSERT_INT32_EQUAL(AssertFact(FormulaGetForm(odd3), FormulaGetActors(odd3), 0), ASSERT_OK)

	// (even 3) is refused: no relation holds (! even 3), but the rule derives it
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetForm(even3), FormulaGetActors(even3), 0), ASSERT_FAIL)
	ASSERT_NULL(RelationRegistryFind(
		FormulaGetForm(even3), FormulaGetActors(even3)->nAtoms, TypedTuplePeekAtomTypes(FormulaGetActors(even3))))

	// (even 4) is accepted, as the rule derives (! even 4) only from (odd 4),
	// which is not a fact
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetForm(even4), FormulaGetActors(even4), 0), ASSERT_OK)

	DictionaryRemoveClause(&entry);
	RetractFact(FormulaGetForm(even4), FormulaGetActors(even4));
	RetractFact(FormulaGetForm(odd3), FormulaGetActors(odd3));
	ReleaseFormula(even4);
	ReleaseFormula(even3);
	ReleaseFormula(odd3);
}


/**
 * A term holding no variable is a fact, and asserting it is asserting that fact.
 */
void testAssertFormulaFact(void)
{
	Atom fact = CStringToTerm("foo \"barf\" bar 1");

	ASSERT_INT32_EQUAL(AssertFormula(fact), ASSERT_OK)
	// the same fact a second time changes nothing
	ASSERT_INT32_EQUAL(AssertFormula(fact), ASSERT_EXISTED)

	// a fact contradicting it is refused, as it is by AssertFact()
	Atom negatedFact = CStringToTerm("! foo \"barf\" bar 1");
	ASSERT_INT32_EQUAL(AssertFormula(negatedFact), ASSERT_FAIL)
	ReleaseFormula(negatedFact);

	RetractFact(FormulaGetForm(fact), FormulaGetActors(fact));
	ReleaseFormula(fact);
}


/**
 * A clause of two or more terms holding a variable is a rule, and asserting it adds the
 * rule to the dictionary.
 */
void testAssertFormulaRule(void)
{
	Atom rule = CStringToClause("before x after y | ! prec x succ y");

	ASSERT_FALSE(DictionaryContainsClause(rule))
	ASSERT_INT32_EQUAL(AssertFormula(rule), ASSERT_OK)
	ASSERT_TRUE(DictionaryContainsClause(rule))

	// the same rule a second time changes nothing
	ASSERT_INT32_EQUAL(AssertFormula(rule), ASSERT_EXISTED)

	// Adding a clause already in the dictionary yields the entry already there,
	// which is what the rule is removed with
	DictionaryEntry entry = DictionaryAddClause(rule);
	DictionaryRemoveClause(&entry);
	ASSERT_FALSE(DictionaryContainsClause(rule))
	ReleaseFormula(rule);
}


/**
 * A formula that is neither a fact nor a rule is refused, and the result code says which
 * of the two it failed to be.
 */
void testAssertFormulaRejects(void)
{
	// a term holding a variable states nothing that could be stored
	Atom termWithVariable = CStringToTerm("foo x bar 1");
	ASSERT_INT32_EQUAL(AssertFormula(termWithVariable), ASSERT_FACT_VARIABLE)
	ReleaseFormula(termWithVariable);

	// a clause holding no variable derives nothing
	Atom groundClause = CStringToClause("foo 1 | ! bar 2");
	ASSERT_INT32_EQUAL(AssertFormula(groundClause), ASSERT_RULE_GROUND)
	ReleaseFormula(groundClause);

	// A clause of one term says no more than that term. The parser never builds one,
	// yielding the term itself instead, so it is built here.
	Atom term = CStringToTerm("foo x bar 1");
	Atom singleTermClause = CreateClause(&term, 1);
	ASSERT_INT32_EQUAL(AssertFormula(singleTermClause), ASSERT_RULE_ONE_TERM)
	ReleaseFormula(singleTermClause);
	ReleaseFormula(term);

	// a conjunction is several rules at once, which this interface does not take
	Atom conjunction = CStringToConjunction("foo x bar 1 | ! baz x & barf 42 frob y");
	ASSERT_INT32_EQUAL(AssertFormula(conjunction), ASSERT_NOT_CLAUSE)
	ReleaseFormula(conjunction);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testAssertRetract);
	ExecuteTest(testAssertContradictsStoredFact);
	ExecuteTest(testAssertContradictsDerivedFact);
	ExecuteTest(testAssertFormulaFact);
	ExecuteTest(testAssertFormulaRule);
	ExecuteTest(testAssertFormulaRejects);

	KernelShutdown();

	TestSummary();
}
