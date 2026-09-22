/**
 * Tests for the high level fact interface, AssertFact() and RetractFact().
 */

#include "kernel/dictionary.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/Relation.h"
#include "lang/formula.h"
#include "library/library.h"
#include "library/list.h"
#include "library/string.h"
#include "parser/ClauseBuilder.h"
#include "parser/ConjunctionBuilder.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"
#include "ui/assert.h"


/**
 * Assert and retract two facts of a relation that does not exist beforehand.
 * The first assert creates the relation and its tuple store.
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

	Relation relation = {.termForm = FormulaGetForm(fact1), .typeSignature = typeSignature};
	// The relation does not exist until the first fact is asserted
	ASSERT_FALSE(RelationExists(relation))
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(fact1), 0), ASSERT_OK)
	ASSERT_TRUE(RelationExists(relation))
	
	ASSERT_UINT32_EQUAL(RelationNRows(relation), 1)

	// Asserting the same fact again changes nothing
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(fact1), 0), ASSERT_EXISTED)
	ASSERT_UINT32_EQUAL(RelationNRows(relation), 1)

	// The second fact goes in the same table
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(fact2), 0), ASSERT_OK)
	ASSERT_UINT32_EQUAL(RelationNRows(relation), 2)

	RetractFact(FormulaGetView(fact2));
	ASSERT_UINT32_EQUAL(RelationNRows(relation), 1)

	RetractFact(FormulaGetView(fact1));

	DropRelation(relation);
	ReleaseFormula(fact2);
	ReleaseFormula(fact1);
}


/**
 * Test asserting a fact that is already produced by a service
 */
void testAssertOverlapsService(void)
{
	// This fact is already provided by the (+ + =) service
	Atom knownFact = CStringToTerm("+ 1 + 1 = 2");

	// Asserting the fact does nothing
	// NOTE: this requires compiling a FILTER service
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(knownFact), 0), ASSERT_EXISTED)
	
	RemoveAllCompiledServices();
	ReleaseFormula(knownFact);
}


/**
 * A fact contradicts the knowledge base when its negation is a tuple of a stored
 * relation, and is refused. Asserting the negation of a stored fact is symmetric,
 * so it does not matter which of the two is asserted first.
 */
void testAssertContradictsStoredFact(void)
{
	// Assert a fact
	Atom fact = CStringToTerm("prec \"a\" succ \"b\"");
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(fact), 0), ASSERT_OK)
	Relation relation = RelationFromFact(FormulaGetView(fact));
	ASSERT_TRUE(RelationExists(relation))

	// (! prec "a" succ "b") is refused, contradicting the fact just asserted
	Atom negatedFact = CStringToTerm("! prec \"a\" succ \"b\"");
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(negatedFact), 0), ASSERT_CONTRADICTION)
	// and the refused assert leaves no relation behind
	Relation negatedRelation = RelationFromFact(FormulaGetView(negatedFact));
	ASSERT_FALSE(RelationExists(negatedRelation))
	
	// Retracting the fact it contradicts makes the same assert succeed
	RetractFact(FormulaGetView(fact));
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(negatedFact), 0), ASSERT_OK)

	// and the positive fact is now the one refused
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(fact), 0), ASSERT_CONTRADICTION)

	RetractFact(FormulaGetView(negatedFact));
	DropRelation(relation);
	DropRelation(negatedRelation);
	ReleaseFormula(negatedFact);
	ReleaseFormula(fact);
}


/**
 * A fact also contradicts the knowledge base when its negation can be derived from a rule.
 * Finding the contradiction then compiles the query for the negated term;
 * see checkContradiction() in assert.c.
 */
void testAssertContradictsDerivedFact(void)
{
	// (! even x) follows from (odd x), so (odd 3) entails (! even 3)
	DictionaryEntry entry = DictionaryAddClauseFromCString("! even x | ! odd x");

	Atom odd3 = CStringToTerm("odd 3");
	FormulaView odd3View = FormulaGetView(odd3);
	ASSERT_INT32_EQUAL(AssertFact(odd3View, 0), ASSERT_OK)

	// (even 3) is refused: no relation holds (! even 3), but the rule derives it
	// from the (odd 3) fact
	Atom even3 = CStringToTerm("even 3");
	FormulaView even3View = FormulaGetView(even3);
	ASSERT_INT32_EQUAL(AssertFact(even3View, 0), ASSERT_CONTRADICTION)
	// no relation was created
	Relation evenRelation = RelationFromFact(even3View);
	ASSERT_FALSE(RelationExists(evenRelation))
	ReleaseFormula(even3);

	// (even 4) is accepted, as the rule derives (! even 4) only from (odd 4),
	// which has not been asserted
	Atom even4 = CStringToTerm("even 4");
	FormulaView even4View = FormulaGetView(even4);
	ASSERT_INT32_EQUAL(AssertFact(even4View, 0), ASSERT_OK)
	ASSERT_TRUE(RelationExists(evenRelation))
	RetractFact(even4View);
	DropRelation(evenRelation);
	ReleaseFormula(even4);

	RetractFact(odd3View);
	DropRelation(RelationFromFact(odd3View));
	ReleaseFormula(odd3);

	DictionaryRemoveClause(&entry);
}


/**
 * A term holding no variable is a fact
 */
void testAssertFormulaFact(void)
{
	Atom fact = CStringToTerm("foo \"barf\" bar 1");

	ASSERT_INT32_EQUAL(AssertFormula(fact), ASSERT_OK)
	// the same fact a second time changes nothing
	ASSERT_INT32_EQUAL(AssertFormula(fact), ASSERT_EXISTED)

	// a fact contradicting it is refused, as it is by AssertFact()
	Atom negatedFact = CStringToTerm("! foo \"barf\" bar 1");
	ASSERT_INT32_EQUAL(AssertFormula(negatedFact), ASSERT_CONTRADICTION)
	ReleaseFormula(negatedFact);

	RetractFact(FormulaGetView(fact));
	DropRelation(RelationFromFact(FormulaGetView(fact)));
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

	// a conjunction is several facts at once, which this interface does not take
	Atom conjunction = CStringToConjunction("foo x bar 1 & barf 42 frob y");
	ASSERT_INT32_EQUAL(AssertFormula(conjunction), ASSERT_NOT_CLAUSE)
	ReleaseFormula(conjunction);
}


/**
 * Test creating an ifact from a conjunction of terms sharing one generator.
 */
void testCreateIFactList(void)
{
	Relation listLetter = GetListRelation(AT_LETTER);
	Relation listLength = GetListLengthRelation();
	size32 listLetterNRows = RelationNRows(listLetter);
	size32 listLengthNRows = RelationNRows(listLength);

	Atom formula = CStringToConjunction(
		"list * position 1 element 'A & list * position 2 element 'B & list * length 2");
	Atom ifact = CreateIFact(FormulaGetView(formula));
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 1)

	// One defining fact per term, in the two relations the terms name
	ASSERT_UINT32_EQUAL(RelationNRows(listLetter), listLetterNRows + 2)
	ASSERT_UINT32_EQUAL(RelationNRows(listLength), listLengthNRows + 1)

	// The defining facts are those of the list ('A 'B), so the atom is that list
	ASSERT_UINT32_EQUAL(ListLength(ifact), 2)
	ASSERT_DATA64_EQUAL(ListGetElement(ifact, 1).hash, GetAlphabetLetter('A').hash)
	ASSERT_DATA64_EQUAL(ListGetElement(ifact, 2).hash, GetAlphabetLetter('B').hash)

	// Creating that list finds the atom already there, and adds no facts
	Atom list = CreateListFromArray(
		(Atom[]) {GetAlphabetLetter('A'), GetAlphabetLetter('B')}, AT_LETTER, 2);
	ASSERT_DATA64_EQUAL(list.hash, ifact.hash)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 2)
	ASSERT_UINT32_EQUAL(RelationNRows(listLetter), listLetterNRows + 2)
	ASSERT_UINT32_EQUAL(RelationNRows(listLength), listLengthNRows + 1)

	// Releasing the last reference retracts the defining facts
	IFactRelease(list);
	IFactRelease(ifact);
	ASSERT_UINT32_EQUAL(RelationNRows(listLetter), listLetterNRows)
	ASSERT_UINT32_EQUAL(RelationNRows(listLength), listLengthNRows)

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

	Atom colourNameTerm = CStringToTerm("colour * name \"red\"");
	Relation colourNameRelation = RelationFromFact(FormulaGetView(colourNameTerm));
	ReleaseFormula(colourNameTerm);
	ASSERT_INT32_EQUAL(RelationNRows(colourNameRelation), 1)

	Atom colourCodeTerm = CStringToTerm("colour * code 4");
	Relation colourCodeRelation = RelationFromFact(FormulaGetView(colourCodeTerm));
	ReleaseFormula(colourCodeTerm);
	ASSERT_INT32_EQUAL(RelationNRows(colourCodeRelation), 1)

	IFactRelease(ifact);
	DropRelation(colourNameRelation);
	DropRelation(colourCodeRelation);
	ReleaseFormula(formula);
}


/**
 * Test creating two ifacts where the defining fact containg a conjunction
 * of terms of the same form, but with the generator (*) in different roles.
 * The two terms are stored in the same table, but belong to different IFactConjunctions.
 */
void testCreateIFactTwoIdColumns(void)
{
	// Both terms are of this form, and both their actors are strings,
	// so the two defining facts belong to one relation
	Atom sameFormTerm = CStringToTerm("pair \"a\" other \"b\"");
	byte atomTypes[2] = {AT_ID, AT_ID};
	TypeSignature typeSignature = CreateTypeSignature(atomTypes, 2);
	
	Relation relation = {.termForm = FormulaGetForm(sameFormTerm), .typeSignature = typeSignature};
	ASSERT_FALSE(RelationExists(relation))

	Atom formula = CStringToConjunction("pair * other \"a\" & pair \"a\" other *");
	Atom ifact = CreateIFact(FormulaGetView(formula));
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 1)

	// Both defining facts are stored in the one relation the two terms share
	ASSERT_TRUE(RelationExists(relation))
	ASSERT_UINT32_EQUAL(RelationNRows(relation), 2)

	// Releasing the atom retracts both facts
	IFactRelease(ifact);
	ASSERT_UINT32_EQUAL(RelationNRows(relation), 0)
	
	DropRelation(relation);
	ReleaseFormula(formula);
	ReleaseFormula(sameFormTerm);
}


/**
 * The same formula a second time yields the atom already defined by those facts,
 * and adds no facts.
 */
void testCreateIFactExisting(void)
{
	Relation listLength = GetListLengthRelation();
	size32 listLengthNRows = RelationNRows(listLength);

	Atom formula = CStringToConjunction("list * position 1 element 'C & list * length 1");
	Atom ifact = CreateIFact(FormulaGetView(formula));
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(RelationNRows(listLength), listLengthNRows + 1)

	Atom sameIFact = CreateIFact(FormulaGetView(formula));
	ASSERT_DATA64_EQUAL(sameIFact.hash, ifact.hash)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 2)
	ASSERT_UINT32_EQUAL(RelationNRows(listLength), listLengthNRows + 1)

	IFactRelease(sameIFact);
	IFactRelease(ifact);
	ASSERT_UINT32_EQUAL(RelationNRows(listLength), listLengthNRows)
	ReleaseFormula(formula);
}


/**
 * A defining fact cannot be retracted; only releasing the atom it defines removes it.
 */
void testCreateIFactDefiningFactsProtected(void)
{
	Relation listLength = GetListLengthRelation();
	size32 listLengthNRows = RelationNRows(listLength);

	Atom formula = CStringToConjunction("list * position 1 element 'D & list * length 1");
	FormulaView formulaView = FormulaGetView(formula);
	Atom ifact = CreateIFact(formulaView);
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(RelationNRows(listLength), listLengthNRows + 1)

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
	ASSERT_UINT32_EQUAL(RelationNRows(listLength), listLengthNRows + 1)

	// Releasing the atom removes it. The defining fact formula holds a reference
	// to the atom it names, so that formula goes first.
	ReleaseFormula(definingFact);
	IFactRelease(ifact);
	ASSERT_UINT32_EQUAL(RelationNRows(listLength), listLengthNRows)

	ReleaseFormula(formula);
}


/**
 * Test create an ifact with a single term containing one generator (*).
 * The corresponding relation is created to hold the fact, and dropped again
 * once the AT_ID atom is released.
 */
void testCreateIFactTerm(void)
{
	Atom term = CStringToTerm("colour * code 4");
	size8 nColumns = FormulaGetActors(term)->nAtoms;

	// The relation of the defining fact carries the identified atom in the generator
	// column, with type AT_ID
	byte atomTypes[2];
	for(index8 i = 0; i < nColumns; i++) {
		TypedAtom actor = TypedTupleGetElement(FormulaGetActors(term), i);
		atomTypes[i] = SameTypedAtoms(actor, generatorAtom) ? AT_ID : actor.type;
	}
	TypeSignature typeSignature = CreateTypeSignature(atomTypes, nColumns);
	Relation relation = {.termForm = FormulaGetForm(term), .typeSignature = typeSignature};
	ASSERT_FALSE(RelationExists(relation))

	Atom ifact = CreateIFact(FormulaGetView(term));
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 1)

	// The defining fact is the only row in the corresponding RelationWriter
	ASSERT_TRUE(RelationExists(relation))
	ASSERT_UINT32_EQUAL(RelationNRows(relation), 1)

	// Releasing the atom retracts the defining fact
	IFactRelease(ifact);
	ASSERT_UINT32_EQUAL(RelationNRows(relation), 0)
	
	// The relation still exists; drop it manually
	ASSERT_TRUE(RelationExists(relation))
	DropRelation(relation);

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
	FormulaView termView = FormulaGetView(term);
	TypedAtom firstActor = TypedTupleGetElement(FormulaGetActors(term), 0);
	ASSERT_FALSE(SameTypedAtoms(firstActor, generatorAtom))

	// CreateIFact() here creates a compiled service using a FILTER operator
	// to find its identifying fact tuples, and creates the relation
	Atom ifact = CreateIFact(termView);
	ASSERT_TRUE(ifact.hash != 0)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 1)
	Relation relation = RelationFromFact(termView);
	ASSERT_TRUE(RelationExists(relation))

	// Calling again retrieves the ID atom already created
	Atom sameIFact = CreateIFact(termView);
	ASSERT_DATA64_EQUAL(sameIFact.hash, ifact.hash)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(ifact), 2)

	IFactRelease(sameIFact);
	IFactRelease(ifact);
	ReleaseFormula(term);
	// The relation is now empty
	ASSERT_INT32_EQUAL(RelationNRows(relation), 0)
	// Dropping the relation also removes the compiled FILTER service
	DropRelation(relation);
}


/**
 * Test creating an ifact from a clause.
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
	ASSERT_TRUE(SameAtoms(termIFact, clauseIFact))
	ASSERT_UINT32_EQUAL(IFactReferenceCount(clauseIFact), 2)
	Relation relation = RelationFromFact(FormulaGetView(term));
	ASSERT_INT32_EQUAL(RelationNRows(relation), 1)

	IFactRelease(termIFact);
	IFactRelease(clauseIFact);
	DropRelation(relation);
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

	// the same two, without a conjunction around them
	Atom termNoGenerator = CStringToTerm("foo 1 bar 2");
	ASSERT_DATA64_EQUAL(CreateIFact(FormulaGetView(termNoGenerator)).hash, 0)
	ReleaseFormula(termNoGenerator);

	Atom termTwoGenerators = CStringToTerm("foo * bar *");
	ASSERT_DATA64_EQUAL(CreateIFact(FormulaGetView(termTwoGenerators)).hash, 0)
	ReleaseFormula(termTwoGenerators);

	// CLAUDE: a clause of two terms is a disjunction, which defines nothing
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
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testAssertRetract);
	ExecuteTest(testAssertOverlapsService);
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

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}
