/**
 * Tests for the high level fact interface, AssertFact() and RetractFact().
 */

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


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testAssertRetract);

	KernelShutdown();

	TestSummary();
}
