/**
 * Tests for the high level fact interface, AssertFact() and RetractFact().
 */

#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "kernel/RelationTableRegistry.h"
#include "lang/Formula.h"
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
	Formula * fact1 = CStringToTerm("foo \"barf\" bar 1");
	Formula * fact2 = CStringToTerm("foo \"baz\" bar 42");
	size8 nColumns = fact1->actors->nAtoms;
	byte const * atomTypes = TypedTuplePeekAtomTypes(fact1->actors);

	// The relation does not exist until the first fact is asserted
	ASSERT_NULL(RelationRegistryFind(fact1->form, nColumns, atomTypes))

	ASSERT_INT32_EQUAL(AssertFact(fact1->form, fact1->actors, 0), ASSERT_OK)

	Relation const * relation = RelationRegistryFind(fact1->form, nColumns, atomTypes);
	ASSERT_NOT_NULL(relation)
	RelationTable const * table = RelationTableRegistryFind(relation);
	ASSERT_NOT_NULL(table)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 1)

	// Asserting the same fact again changes nothing
	ASSERT_INT32_EQUAL(AssertFact(fact1->form, fact1->actors, 0), ASSERT_EXISTED)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 1)

	// The second fact goes in the same table
	ASSERT_INT32_EQUAL(AssertFact(fact2->form, fact2->actors, 0), ASSERT_OK)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 2)

	RetractFact(fact2->form, fact2->actors);
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 1)

	// Retracting the last fact drops the table, and the relation with it
	RetractFact(fact1->form, fact1->actors);
	ASSERT_NULL(RelationRegistryFind(fact1->form, nColumns, atomTypes))

	FreeFormula(fact2);
	FreeFormula(fact1);
}


/**
 * A fact contradicts the knowledge base when its negation is a tuple of a stored
 * relation, and is refused. Asserting the negation of a stored fact is symmetric,
 * so it does not matter which of the two is asserted first.
 */
void testAssertContradictsStoredFact(void)
{
	Formula * fact = CStringToTerm("prec \"a\" succ \"b\"");
	Formula * negatedFact = CStringToTerm("! prec \"a\" succ \"b\"");
	size8 nColumns = negatedFact->actors->nAtoms;
	byte const * atomTypes = TypedTuplePeekAtomTypes(negatedFact->actors);

	ASSERT_INT32_EQUAL(AssertFact(fact->form, fact->actors, 0), ASSERT_OK)

	// (! prec "a" succ "b") is refused, contradicting the fact just asserted
	ASSERT_INT32_EQUAL(AssertFact(negatedFact->form, negatedFact->actors, 0), ASSERT_FAIL)
	// and the refused assert leaves no relation behind
	ASSERT_NULL(RelationRegistryFind(negatedFact->form, nColumns, atomTypes))

	// Retracting the fact it contradicts makes the same assert succeed
	RetractFact(fact->form, fact->actors);
	ASSERT_INT32_EQUAL(AssertFact(negatedFact->form, negatedFact->actors, 0), ASSERT_OK)

	// and the positive fact is now the one refused
	ASSERT_INT32_EQUAL(AssertFact(fact->form, fact->actors, 0), ASSERT_FAIL)

	RetractFact(negatedFact->form, negatedFact->actors);
	FreeFormula(negatedFact);
	FreeFormula(fact);
}


/**
 * A fact also contradicts the knowledge base when its negation is not stored, but
 * derived by a rule. Finding the contradiction then compiles the query for the
 * negated term; see checkContradiction() in assert.c.
 */
void testAssertContradictsDerivedFact(void)
{
	// (! even x) follows from (odd x), so (odd 3) entails (! even 3)
	DictionaryEntry entry = DictionaryAddClauseFromCString("! even _x | ! odd _x");
	Formula * odd3 = CStringToTerm("odd 3");
	Formula * even3 = CStringToTerm("even 3");
	Formula * even4 = CStringToTerm("even 4");

	ASSERT_INT32_EQUAL(AssertFact(odd3->form, odd3->actors, 0), ASSERT_OK)

	// (even 3) is refused: no relation holds (! even 3), but the rule derives it
	ASSERT_INT32_EQUAL(AssertFact(even3->form, even3->actors, 0), ASSERT_FAIL)
	ASSERT_NULL(RelationRegistryFind(
		even3->form, even3->actors->nAtoms, TypedTuplePeekAtomTypes(even3->actors)))

	// (even 4) is accepted, as the rule derives (! even 4) only from (odd 4),
	// which is not a fact
	ASSERT_INT32_EQUAL(AssertFact(even4->form, even4->actors, 0), ASSERT_OK)

	DictionaryRemoveClause(&entry);
	RetractFact(even4->form, even4->actors);
	RetractFact(odd3->form, odd3->actors);
	FreeFormula(even4);
	FreeFormula(even3);
	FreeFormula(odd3);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testAssertRetract);
	ExecuteTest(testAssertContradictsStoredFact);
	ExecuteTest(testAssertContradictsDerivedFact);

	KernelShutdown();

	TestSummary();
}
