/**
 * Tests for the high level fact interface, AssertFact() and RetractFact().
 */

#include "kernel/dictionary.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/list.h"
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
	TypeSignature typeSignature = CreateTypeSignature(
		TypedTuplePeekAtomTypes(FormulaGetActors(fact1)), nColumns);

	// The relation does not exist until the first fact is asserted
	ASSERT_NULL(RelationRegistryFind(FormulaGetForm(fact1), nColumns, typeSignature))

	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(fact1), 0), ASSERT_OK)

	Relation const * relation = RelationRegistryFind(FormulaGetForm(fact1), nColumns, typeSignature);
	ASSERT_NOT_NULL(relation)
	RelationTable const * table = RelationTableRegistryFind(relation);
	ASSERT_NOT_NULL(table)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 1)

	// Asserting the same fact again changes nothing
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(fact1), 0), ASSERT_EXISTED)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 1)

	// The second fact goes in the same table
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(fact2), 0), ASSERT_OK)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 2)

	RetractFact(FormulaGetView(fact2));
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 1)

	// Retracting the last fact drops the table, and the relation with it
	RetractFact(FormulaGetView(fact1));
	ASSERT_NULL(RelationRegistryFind(FormulaGetForm(fact1), nColumns, typeSignature))

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
	TypeSignature typeSignature = CreateTypeSignature(
		TypedTuplePeekAtomTypes(FormulaGetActors(negatedFact)), nColumns);

	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(fact), 0), ASSERT_OK)

	// (! prec "a" succ "b") is refused, contradicting the fact just asserted
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(negatedFact), 0), ASSERT_FAIL)
	// and the refused assert leaves no relation behind
	ASSERT_NULL(RelationRegistryFind(FormulaGetForm(negatedFact), nColumns, typeSignature))

	// Retracting the fact it contradicts makes the same assert succeed
	RetractFact(FormulaGetView(fact));
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(negatedFact), 0), ASSERT_OK)

	// and the positive fact is now the one refused
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(fact), 0), ASSERT_FAIL)

	RetractFact(FormulaGetView(negatedFact));
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

	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(odd3), 0), ASSERT_OK)

	// (even 3) is refused: no relation holds (! even 3), but the rule derives it
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(even3), 0), ASSERT_FAIL)
	ASSERT_NULL(RelationRegistryFind(
		FormulaGetForm(even3), FormulaGetActors(even3)->nAtoms,
		CreateTypeSignature(
			TypedTuplePeekAtomTypes(FormulaGetActors(even3)),
			FormulaGetActors(even3)->nAtoms)))

	// (even 4) is accepted, as the rule derives (! even 4) only from (odd 4),
	// which is not a fact
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(even4), 0), ASSERT_OK)

	DictionaryRemoveClause(&entry);
	RetractFact(FormulaGetView(even4));
	RetractFact(FormulaGetView(odd3));
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

	RetractFact(FormulaGetView(fact));
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
	ASSERT_INT32_EQUAL(AssertFormula(termWithVariable), ASSERT_TERM_VARIABLE)
	ReleaseFormula(termWithVariable);

	// a clause holding no variable derives nothing
	Atom groundClause = CStringToClause("foo 1 | ! bar 2");
	ASSERT_INT32_EQUAL(AssertFormula(groundClause), ASSERT_CLAUSE_NO_VARIABLE)
	ReleaseFormula(groundClause);

	// A clause of one term says no more than that term. The parser never builds one,
	// yielding the term itself instead, so it is built here.
	Atom term = CStringToTerm("foo x bar 1");
	Atom singleTermClause = CreateClause(&term, 1);
	ASSERT_INT32_EQUAL(AssertFormula(singleTermClause), ASSERT_CLAUSE_ONE_TERM)
	ReleaseFormula(singleTermClause);
	ReleaseFormula(term);

	// a conjunction is several rules at once, which this interface does not take
	Atom conjunction = CStringToConjunction("foo x bar 1 | ! baz x & barf 42 frob y");
	ASSERT_INT32_EQUAL(AssertFormula(conjunction), ASSERT_NOT_CLAUSE)
	ReleaseFormula(conjunction);
}


/**
 * A conjunction of terms sharing one generator (*) defines an atom. The terms here are
 * the ones a list is built from, so the atom CreateIFact() returns is the list ('A 'B),
 * and CreateListFromArray() yields that same atom.
 */
void testCreateIFactList(void)
{
	RelationTable const * listLetter = GetCoreRelationTable(RELATION_LIST_LETTER);
	RelationTable const * listLength = GetCoreRelationTable(RELATION_LIST_LENGTH);
	size32 listLetterNRows = RelationTableNRows(listLetter);
	size32 listLengthNRows = RelationTableNRows(listLength);

	Atom formula = CStringToConjunction(
		"list * position 1 element 'A & list * position 2 element 'B & list * length 2");
	Atom ifact = CreateIFact(FormulaGetView(formula));
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 1)

	// One defining fact per term, in the two relations the terms name
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLetter), listLetterNRows + 2)
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLength), listLengthNRows + 1)

	// The defining facts are those of the list ('A 'B), so the atom is that list
	ASSERT_UINT32_EQUAL(ListLength(ifact), 2)
	ASSERT_DATA64_EQUAL(ListGetElement(ifact, 1).hash, GetAlphabetLetter('A').hash)
	ASSERT_DATA64_EQUAL(ListGetElement(ifact, 2).hash, GetAlphabetLetter('B').hash)

	// Creating that list finds the atom already there, and adds no facts
	Atom list = CreateListFromArray(
		(Atom[]) {GetAlphabetLetter('A'), GetAlphabetLetter('B')}, AT_LETTER, 2);
	ASSERT_DATA64_EQUAL(list.hash, ifact.hash)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 2)
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLetter), listLetterNRows + 2)
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLength), listLengthNRows + 1)

	// Releasing the last reference retracts the defining facts
	IFactRelease(list);
	IFactRelease(ifact);
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLetter), listLetterNRows)
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLength), listLengthNRows)

	ReleaseFormula(formula);
}


/**
 * A conjunction whose terms name relations that do not exist beforehand.
 * Each relation and its table are created to hold the defining facts.
 */
void testCreateIFactNewRelations(void)
{
	Atom formula = CStringToConjunction("colour * name \"red\" & colour * code 4");
	Atom ifact = CreateIFact(FormulaGetView(formula));
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 1)

	IFactRelease(ifact);
	ReleaseFormula(formula);
}


/**
 * Two terms of one relation, each with the generator (*) in a different role.
 * The two defining facts are stored in the same table, but identify the atom by
 * different columns, so each becomes a conjunction of its own.
 */
void testCreateIFactTwoIdColumns(void)
{
	// Both terms are of this form, and both their actors are strings,
	// so the two defining facts belong to one relation
	Atom sameFormTerm = CStringToTerm("pair \"a\" other \"b\"");
	byte atomTypes[2] = {AT_ID, AT_ID};
	TypeSignature typeSignature = CreateTypeSignature(atomTypes, 2);
	ASSERT_NULL(RelationRegistryFind(FormulaGetForm(sameFormTerm), 2, typeSignature))

	Atom formula = CStringToConjunction("pair * other \"a\" & pair \"a\" other *");
	Atom ifact = CreateIFact(FormulaGetView(formula));
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 1)

	// Both defining facts are stored in the one relation the two terms share
	Relation const * relation = RelationRegistryFind(FormulaGetForm(sameFormTerm), 2, typeSignature);
	ASSERT_NOT_NULL(relation)
	RelationTable const * table = RelationTableRegistryFind(relation);
	ASSERT_NOT_NULL(table)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 2)

	// Releasing the atom retracts both facts, which drops the table
	IFactRelease(ifact);
	ASSERT_NULL(RelationRegistryFind(FormulaGetForm(sameFormTerm), 2, typeSignature))

	ReleaseFormula(formula);
	ReleaseFormula(sameFormTerm);
}


/**
 * The same formula a second time yields the atom already defined by those facts,
 * and adds no facts.
 */
void testCreateIFactExisting(void)
{
	RelationTable const * listLength = GetCoreRelationTable(RELATION_LIST_LENGTH);
	size32 listLengthNRows = RelationTableNRows(listLength);

	Atom formula = CStringToConjunction("list * position 1 element 'C & list * length 1");
	Atom ifact = CreateIFact(FormulaGetView(formula));
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLength), listLengthNRows + 1)

	Atom sameIFact = CreateIFact(FormulaGetView(formula));
	ASSERT_DATA64_EQUAL(sameIFact.hash, ifact.hash)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 2)
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLength), listLengthNRows + 1)

	IFactRelease(sameIFact);
	IFactRelease(ifact);
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLength), listLengthNRows)
	ReleaseFormula(formula);
}


/**
 * A defining fact cannot be retracted; only releasing the atom it defines removes it.
 */
void testCreateIFactDefiningFactsProtected(void)
{
	RelationTable const * listLength = GetCoreRelationTable(RELATION_LIST_LENGTH);
	size32 listLengthNRows = RelationTableNRows(listLength);

	Atom formula = CStringToConjunction("list * position 1 element 'D & list * length 1");
	FormulaView formulaView = FormulaGetView(formula);
	Atom ifact = CreateIFact(formulaView);
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLength), listLengthNRows + 1)

	// Build the (list length) term the ifact defines, by putting the identified atom
	// where the generator stands. The conjunction holds the actors of both its terms,
	// so the length term is found by its own form.
	Atom lengthTerm = CStringToTerm("list \"x\" length 1");
	size8 arity = FormulaGetActors(lengthTerm)->nAtoms;
	TypedTuple * actors = CreateTypedTuple(arity);
	for(index8 i = 0; i < arity; i++) {
		TypedAtom actor = TypedTupleGetElement(formulaView.actors, formulaView.actors->nAtoms - arity + i);
		TypedTupleSetElement(
			actors, i,
			SameTypedAtoms(actor, generatorAtom) ? CreateTypedAtom(AT_ID, ifact) : actor);
	}
	Atom definingFact = CreateFormula(FormulaGetForm(lengthTerm), actors);
	FreeTypedTuple(actors);
	ReleaseFormula(lengthTerm);

	// Retracting it leaves it in place
	RetractFact(FormulaGetView(definingFact));
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLength), listLengthNRows + 1)

	// Releasing the atom removes it. The defining fact formula holds a reference
	// to the atom it names, so that formula goes first.
	ReleaseFormula(definingFact);
	IFactRelease(ifact);
	ASSERT_UINT32_EQUAL(RelationTableNRows(listLength), listLengthNRows)

	ReleaseFormula(formula);
}


/**
 * A term of one generator (*) defines an atom, without a clause or conjunction
 * around it. The relation holding the defining fact is created to hold it,
 * and dropped again once the atom is released.
 */
void testCreateIFactTerm(void)
{
	Atom term = CStringToTerm("colour * code 4");
	size8 nColumns = FormulaGetActors(term)->nAtoms;

	// The relation of the defining fact carries the identified atom in the generator
	// column, so its type there is AT_ID rather than the generator's own type
	byte atomTypes[2];
	for(index8 i = 0; i < nColumns; i++) {
		TypedAtom actor = TypedTupleGetElement(FormulaGetActors(term), i);
		atomTypes[i] = SameTypedAtoms(actor, generatorAtom) ? AT_ID : actor.type;
	}
	TypeSignature typeSignature = CreateTypeSignature(atomTypes, nColumns);
	ASSERT_NULL(RelationRegistryFind(FormulaGetForm(term), nColumns, typeSignature))

	Atom ifact = CreateIFact(FormulaGetView(term));
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 1)

	// The defining fact is the one row of the relation the term names
	Relation const * relation = RelationRegistryFind(FormulaGetForm(term), nColumns, typeSignature);
	ASSERT_NOT_NULL(relation)
	RelationTable const * table = RelationTableRegistryFind(relation);
	ASSERT_NOT_NULL(table)
	ASSERT_UINT32_EQUAL(RelationTableNRows(table), 1)

	// Releasing the atom retracts the defining fact, which drops the table
	// and the relation with it
	IFactRelease(ifact);
	ASSERT_NULL(RelationRegistryFind(FormulaGetForm(term), nColumns, typeSignature))

	ReleaseFormula(term);
}


/**
 * A term whose generator (*) is not the first actor of its form. The order of the
 * actors follows the role names of the form, not the order they were written in,
 * so the identified atom can land in any column.
 */
void testCreateIFactIdColumnNotFirst(void)
{
	// The roles of this form order as (alpha zebra), so the generator is the second actor
	Atom term = CStringToTerm("zebra * alpha 1");
	TypedAtom firstActor = TypedTupleGetElement(FormulaGetActors(term), 0);
	ASSERT_FALSE(SameTypedAtoms(firstActor, generatorAtom))

	Atom ifact = CreateIFact(FormulaGetView(term));
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 1)

	// The same term a second time compares the defining fact against the one stored,
	// which reads the table by the identified column as well
	Atom sameIFact = CreateIFact(FormulaGetView(term));
	ASSERT_DATA64_EQUAL(sameIFact.hash, ifact.hash)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 2)

	IFactRelease(sameIFact);
	IFactRelease(ifact);
	ReleaseFormula(term);
}


/**
 * A clause of one term defines the atom that term defines. The clause form differs
 * from the term form, but the defining fact is the same fact, so both formulas
 * yield the same atom.
 */
void testCreateIFactClause(void)
{
	Atom term = CStringToTerm("colour * code 7");
	Atom clause = CStringToClause("colour * code 7");
	ASSERT_TRUE(FormulaIsTerm(term))
	ASSERT_TRUE(FormulaIsClause(clause))

	Atom clauseIFact = CreateIFact(FormulaGetView(clause));
	ASSERT_TRUE(clauseIFact.hash != 0)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(clauseIFact), 1)

	// The term states the same fact, so it defines the atom already there
	Atom termIFact = CreateIFact(FormulaGetView(term));
	ASSERT_DATA64_EQUAL(termIFact.hash, clauseIFact.hash)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(clauseIFact), 2)

	IFactRelease(termIFact);
	IFactRelease(clauseIFact);
	ReleaseFormula(clause);
	ReleaseFormula(term);
}


/**
 * A formula that does not define an atom yields the zero atom.
 */
void testCreateIFactRejects(void)
{
	// a term with no generator says nothing about the atom being defined
	Atom noGenerator = CStringToConjunction("list * length 1 & foo 1 bar 2");
	ASSERT_DATA64_EQUAL(CreateIFact(FormulaGetView(noGenerator)).hash, 0)
	ReleaseFormula(noGenerator);

	// two generators in one term leave it unclear which one is being defined
	Atom twoGenerators = CStringToConjunction("list * length 1 & foo * bar *");
	ASSERT_DATA64_EQUAL(CreateIFact(FormulaGetView(twoGenerators)).hash, 0)
	ReleaseFormula(twoGenerators);

	// a clause of two terms is a disjunction, which defines nothing
	Atom disjunction = CStringToConjunction("list * length 1 & foo * bar 1 | baz * qux 2");
	ASSERT_DATA64_EQUAL(CreateIFact(FormulaGetView(disjunction)).hash, 0)
	ReleaseFormula(disjunction);

	// the same three, without a conjunction around them
	Atom termNoGenerator = CStringToTerm("foo 1 bar 2");
	ASSERT_DATA64_EQUAL(CreateIFact(FormulaGetView(termNoGenerator)).hash, 0)
	ReleaseFormula(termNoGenerator);

	Atom termTwoGenerators = CStringToTerm("foo * bar *");
	ASSERT_DATA64_EQUAL(CreateIFact(FormulaGetView(termTwoGenerators)).hash, 0)
	ReleaseFormula(termTwoGenerators);

	Atom clauseDisjunction = CStringToClause("foo * bar 1 | baz * qux 2");
	ASSERT_DATA64_EQUAL(CreateIFact(FormulaGetView(clauseDisjunction)).hash, 0)
	ReleaseFormula(clauseDisjunction);

	// a clause of one term with no generator defines nothing either
	Atom clauseNoGenerator = CStringToClause("foo 1 bar 2");
	ASSERT_DATA64_EQUAL(CreateIFact(FormulaGetView(clauseNoGenerator)).hash, 0)
	ReleaseFormula(clauseNoGenerator);
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
	ExecuteTest(testCreateIFactTerm);
	ExecuteTest(testCreateIFactIdColumnNotFirst);
	ExecuteTest(testCreateIFactClause);
	ExecuteTest(testCreateIFactList);
	ExecuteTest(testCreateIFactNewRelations);
	ExecuteTest(testCreateIFactTwoIdColumns);
	ExecuteTest(testCreateIFactExisting);
	ExecuteTest(testCreateIFactDefiningFactsProtected);
	ExecuteTest(testCreateIFactRejects);

	KernelShutdown();

	TestSummary();
}
