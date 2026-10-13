/**
 * Tests for the high level fact interface, AssertFact() and RetractFact().
 */

#include "kernel/ClosedRelation.h"
#include "kernel/dictionary.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/TypedRelation.h"
#include "lang/name.h"
#include "lang/formula.h"
#include "library/library.h"
#include "library/list.h"
#include "library/string.h"
#include "parser/ClauseBuilder.h"
#include "parser/ConjunctionBuilder.h"
#include "parser/FormulaBuilder.h"
#include "parser/TermBuilder.h"
#include "parser/TermFormBuilder.h"
#include "testing/testing.h"
#include "ui/assert.h"
#include "ui/load.h"
#include "ui/query.h"


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

	TypedRelation relation = {.form = FormulaGetForm(fact1), .typeSignature = typeSignature};
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
	TypedRelation relation = RelationFromFact(FormulaGetView(fact));
	ASSERT_TRUE(RelationExists(relation))

	// (! prec "a" succ "b") is refused, contradicting the fact just asserted
	Atom negatedFact = CStringToTerm("! prec \"a\" succ \"b\"");
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(negatedFact), 0), ASSERT_CONTRADICTION)
	// and the refused assert leaves no relation behind
	TypedRelation negatedRelation = RelationFromFact(FormulaGetView(negatedFact));
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
 * Asserting (closed-relation [. even]) closes the relation (even x), which then rejects
 * a new fact, since the negation of the fact is inferred. This holds also for a typed relation
 * that does not exist, such as (even x<ID). Retracting (closed-relation [. even]) opens the
 * relation, which then accepts the fact.
 */
void testAssertClosedRelation(void)
{
	Atom even2 = CStringToTerm("even 2");
	Atom even6 = CStringToTerm("even 6");
	Atom evenFoo = CStringToTerm("even \"foo\"");
	Atom notEven3 = CStringToTerm("! even 3");
	Atom notEven2 = CStringToTerm("! even 2");
	Atom closedEven = CStringToTerm("closed-relation [. even]");
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(even2), 0), ASSERT_OK)
	TypedRelation relation = RelationFromFact(FormulaGetView(even2));
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(closedEven), 0), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(closedEven), 0), ASSERT_EXISTED)
	ASSERT_TRUE(RelationIsClosed(FormulaGetForm(even2)))

	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(even6), 0), ASSERT_CONTRADICTION)
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(evenFoo), 0), ASSERT_CONTRADICTION)
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(notEven3), 0), ASSERT_EXISTED)
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(notEven2), 0), ASSERT_CONTRADICTION)
	ASSERT_UINT32_EQUAL(RelationNRows(relation), 1)

	// CLAUDE: The opened relation accepts the fact
	RetractFact(FormulaGetView(closedEven));
	ASSERT_FALSE(RelationIsClosed(FormulaGetForm(even2)))
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(even6), 0), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(closedEven), 0), ASSERT_OK)
	ASSERT_UINT32_EQUAL(RelationNRows(relation), 2)
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(even6), 0), ASSERT_EXISTED)

	RetractFact(FormulaGetView(closedEven));
	RetractFact(FormulaGetView(even6));
	RetractFact(FormulaGetView(even2));
	DropRelation(relation);
	ReleaseFormula(closedEven);
	ReleaseFormula(notEven2);
	ReleaseFormula(notEven3);
	ReleaseFormula(evenFoo);
	ReleaseFormula(even6);
	ReleaseFormula(even2);
}


/**
 * The relations (+ + =) and (integer x) are closed by MathSetup(), and reject the facts
 * (+ 1 + 1 = 3) and (integer "foo"), since the negation of each fact is inferred.
 */
void testAssertClosedComputedRelation(void)
{
	Atom sum = CStringToTerm("+ 1 + 1 = 3");
	Atom integer = CStringToTerm("integer \"foo\"");
	ASSERT_TRUE(RelationIsClosed(FormulaGetForm(sum)))
	ASSERT_TRUE(RelationIsClosed(FormulaGetForm(integer)))
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(sum), 0), ASSERT_CONTRADICTION)
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(integer), 0), ASSERT_CONTRADICTION)
	ReleaseFormula(integer);
	ReleaseFormula(sum);
}


/**
 * CLAUDE: A relation cannot be closed unless the actor of (closed-relation f) is a term form,
 * and the opposite relation is neither closed nor stored; see CloseRelation(). A rule cannot
 * derive (closed-relation f).
 */
void testAssertClosedRelationRejected(void)
{
	Atom closedInt = CStringToTerm("closed-relation 3");
	Atom closedString = CStringToTerm("closed-relation \"even\"");
	Atom closedEven = CStringToTerm("closed-relation [. even]");
	Atom closedNotEven = CStringToTerm("closed-relation [. ! even]");
	Atom notOdd3 = CStringToTerm("! odd 3");
	Atom closedOdd = CStringToTerm("closed-relation [. odd]");
	Atom rule = CStringToClause("closed-relation f | ! foo f");

	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(closedInt), 0), ASSERT_NOT_CLOSABLE)
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(closedString), 0), ASSERT_NOT_CLOSABLE)

	// CLAUDE: The opposite relation is closed
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(closedEven), 0), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(closedNotEven), 0), ASSERT_NOT_CLOSABLE)
	RetractFact(FormulaGetView(closedEven));

	// CLAUDE: The opposite relation is stored
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(notOdd3), 0), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(closedOdd), 0), ASSERT_NOT_CLOSABLE)
	TypedRelation notOddRelation = RelationFromFact(FormulaGetView(notOdd3));
	RetractFact(FormulaGetView(notOdd3));
	DropRelation(notOddRelation);

	ASSERT_INT32_EQUAL(AssertFormula(rule), ASSERT_RESERVED_FORM)

	ReleaseFormula(rule);
	ReleaseFormula(closedOdd);
	ReleaseFormula(notOdd3);
	ReleaseFormula(closedNotEven);
	ReleaseFormula(closedEven);
	ReleaseFormula(closedString);
	ReleaseFormula(closedInt);
}


/**
 * A fact also contradicts the knowledge base when its negation can be derived from a rule.
 * Finding the contradiction then compiles the query for the negated term;
 * see checkContradiction() in assert.c.
 */
void testAssertContradictsDerivedFact(void)
{
	// (! even x) follows from (odd x), so (odd 3) entails (! even 3)
	FormulaView clause = DictionaryAddClauseFromCString("! even x | ! odd x");

	Atom odd3 = CStringToTerm("odd 3");
	FormulaView odd3View = FormulaGetView(odd3);
	ASSERT_INT32_EQUAL(AssertFact(odd3View, 0), ASSERT_OK)

	// (even 3) is refused: no relation holds (! even 3), but the rule derives it
	// from the (odd 3) fact
	Atom even3 = CStringToTerm("even 3");
	FormulaView even3View = FormulaGetView(even3);
	ASSERT_INT32_EQUAL(AssertFact(even3View, 0), ASSERT_CONTRADICTION)
	// no relation was created
	TypedRelation evenRelation = RelationFromFact(even3View);
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

	DictionaryRemoveClause(&clause);
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
	FormulaView clause = DictionaryAddClause(rule);
	DictionaryRemoveClause(&clause);
	ASSERT_FALSE(DictionaryContainsClause(rule))
	ReleaseFormula(rule);
}


/**
 * A term holding a generator is an ifact rule, and asserting it adds the rule
 * to the dictionary. A formula with a generator that is not an ifact rule is refused.
 */
void testAssertFormulaIFactRule(void)
{
	Atom rule = CStringToTerm("circle * radius r");
	ASSERT_INT32_EQUAL(AssertFormula(rule), ASSERT_OK)
	ASSERT_TRUE(DictionaryContainsIFactRule(rule))
	ASSERT_INT32_EQUAL(AssertFormula(rule), ASSERT_EXISTED)

	Atom invalidRule = CStringToTerm("circle * radius 5.0");
	ASSERT_INT32_EQUAL(AssertFormula(invalidRule), ASSERT_INVALID_IFACT)
	ReleaseFormula(invalidRule);

	FormulaView ifactRule = DictionaryAddIFactRule(rule);
	DictionaryRemoveIFactRule(&ifactRule);
	ReleaseFormula(rule);
}


/**
 * CLAUDE: Retracting a rule removes it from the dictionary and invalidates the services
 * compiled from it. The rule (! even x | ! odd x) makes (even 3) contradict (odd 3);
 * see testAssertContradictsDerivedFact(). Once the rule is retracted, (even 3) can be asserted.
 */
void testRetractFormulaRule(void)
{
	Atom rule = CStringToClause("! even x | ! odd x");
	ASSERT_INT32_EQUAL(AssertFormula(rule), ASSERT_OK)

	Atom odd3 = CStringToTerm("odd 3");
	FormulaView odd3View = FormulaGetView(odd3);
	ASSERT_INT32_EQUAL(AssertFact(odd3View, 0), ASSERT_OK)
	Atom even3 = CStringToTerm("even 3");
	FormulaView even3View = FormulaGetView(even3);
	ASSERT_INT32_EQUAL(AssertFact(even3View, 0), ASSERT_CONTRADICTION)

	ASSERT_INT32_EQUAL(RetractFormula(rule), RETRACT_OK)
	ASSERT_FALSE(DictionaryContainsClause(rule))
	ASSERT_INT32_EQUAL(RetractFormula(rule), RETRACT_NOT_FOUND)
	ASSERT_INT32_EQUAL(AssertFact(even3View, 0), ASSERT_OK)

	// a clause with other variable names is a different clause; see DictionaryAddClause()
	Atom renamedRule = CStringToClause("! even y | ! odd y");
	ASSERT_INT32_EQUAL(RetractFormula(renamedRule), RETRACT_NOT_FOUND)
	ReleaseFormula(renamedRule);

	ASSERT_INT32_EQUAL(RetractFormula(even3), RETRACT_OK)
	DropRelation(RelationFromFact(even3View));
	ReleaseFormula(even3);
	ASSERT_INT32_EQUAL(RetractFormula(odd3), RETRACT_OK)
	DropRelation(RelationFromFact(odd3View));
	ReleaseFormula(odd3);
	ReleaseFormula(rule);
}


/**
 * CLAUDE: An ifact rule is retracted whatever its variable names, since the dictionary
 * compares ifact rules by term form and generator position only.
 */
void testRetractFormulaIFactRule(void)
{
	Atom rule = CStringToTerm("circle * radius r");
	ASSERT_INT32_EQUAL(AssertFormula(rule), ASSERT_OK)

	Atom renamedRule = CStringToTerm("circle * radius s");
	ASSERT_INT32_EQUAL(RetractFormula(renamedRule), RETRACT_OK)
	ASSERT_FALSE(DictionaryContainsIFactRule(rule))
	ASSERT_INT32_EQUAL(RetractFormula(rule), RETRACT_NOT_FOUND)
	ReleaseFormula(renamedRule);
	ReleaseFormula(rule);

	Atom invalidRule = CStringToTerm("circle * radius 5.0");
	ASSERT_INT32_EQUAL(RetractFormula(invalidRule), RETRACT_INVALID_IFACT)
	ReleaseFormula(invalidRule);
}


/**
 * CLAUDE: A term with variables is neither a fact nor an ifact rule, and a conjunction
 * is neither a fact nor a rule.
 */
void testRetractFormulaRejects(void)
{
	Atom termWithVariable = CStringToTerm("foo x bar 1");
	ASSERT_INT32_EQUAL(RetractFormula(termWithVariable), RETRACT_TERM_VARIABLE)
	ReleaseFormula(termWithVariable);

	Atom conjunction = CStringToConjunction("foo x & bar x");
	ASSERT_INT32_EQUAL(RetractFormula(conjunction), RETRACT_NOT_CLAUSE)
	ReleaseFormula(conjunction);
}


/**
 * CLAUDE: A user may retract a rule added by MathSetup(), or open a relation that
 * MathSetup() closed. The test asserts both again, which leaves dictionary entries that
 * MathShutdown() did not create. MathShutdown() must then still find and remove them.
 */
void testRetractMathRule(void)
{
	Atom rule = CStringToClause("+ x + y = z | ! + z - x = y");
	ASSERT_INT32_EQUAL(RetractFormula(rule), RETRACT_OK)
	ASSERT_FALSE(DictionaryContainsClause(rule))
	ASSERT_INT32_EQUAL(AssertFormula(rule), ASSERT_OK)
	ReleaseFormula(rule);

	Atom closedInteger = CStringToTerm("closed-relation [. integer]");
	ASSERT_INT32_EQUAL(RetractFormula(closedInteger), RETRACT_OK)
	Atom integer = CStringToTerm("integer 1");
	ASSERT_FALSE(RelationIsClosed(FormulaGetForm(integer)))

	ASSERT_INT32_EQUAL(AssertFormula(closedInteger), ASSERT_OK)
	ASSERT_TRUE(RelationIsClosed(FormulaGetForm(integer)))
	ReleaseFormula(integer);
	ReleaseFormula(closedInteger);
}


/*
 * CLAUDE: Run a query with UserQuery() and return the number of facts found.
 * The query may be a term or a conjunction.
 */
static size32 countQueryFacts(char const * queryString)
{
	Atom query = CStringToFormula(queryString);
	MixedTypeRelation * relation = UserQuery(FormulaGetView(query));
	size32 nFacts = 0;
	while(MixedTypeRelationNext(relation))
		nFacts++;
	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);
	return nFacts;
}


/**
 * CLAUDE: Asserting (circle "c" radius -3) compiles a query for (< -3 > 0) while checking for a
 * contradiction, before the relation (circle radius) exists. The compiled service then has no
 * part from the rule. Creating the relation must invalidate the service, so that the next query
 * for (< -3 > 0) is compiled again from the rule; see InvalidateDependentServices().
 */
void testAssertFactAfterRule(void)
{
	Atom rule = CStringToClause("! circle c radius r | < r > 0");
	ASSERT_INT32_EQUAL(AssertFormula(rule), ASSERT_OK)
	Atom circle = CStringToTerm("circle \"c\" radius -3");
	ASSERT_INT32_EQUAL(AssertFormula(circle), ASSERT_OK)

	// (< -3 > 0) is derived from the rule, and (< 5 > 0) is computed by the MACHINE service
	ASSERT_UINT32_EQUAL(countQueryFacts("< -3 > 0"), 1)
	ASSERT_UINT32_EQUAL(countQueryFacts("< 5 > 0"), 1)
	ASSERT_UINT32_EQUAL(countQueryFacts("< -5 > 0"), 0)

	ASSERT_INT32_EQUAL(RetractFormula(rule), RETRACT_OK)
	ASSERT_UINT32_EQUAL(countQueryFacts("< -3 > 0"), 0)
	TypedRelation circleRelation = RelationFromFact(FormulaGetView(circle));
	RetractFact(FormulaGetView(circle));
	DropRelation(circleRelation);
	ReleaseFormula(circle);
	ReleaseFormula(rule);
}


/*
 * CLAUDE: Drop every relation of the form of the given term.
 */
static void dropFormRelations(char const * termString)
{
	Atom term = CStringToTerm(termString);
	ResizingArray relations;
	CreateResizingArray(&relations, sizeof(TypedRelation), 4);
	RelationIterator iterator;
	RelationRegistryIterate(FormulaGetForm(term), &iterator);
	while(RelationIteratorNext(&iterator)) {
		TypedRelation relation = RelationIteratorGet(&iterator);
		ResizingArrayAppend(&relations, &relation);
	}
	RelationIteratorEnd(&iterator);
	for(index32 i = 0; i < relations.nElements; i++)
		DropRelation(*(TypedRelation *) ResizingArrayGetElement(&relations, i));
	FreeResizingArray(&relations);
	ReleaseFormula(term);
}


/**
 * CLAUDE: The query (huge c) is compiled before the relation (circle radius) exists, so the
 * rule for (big c) yields nothing, and (huge c) keeps no operator reading (big c). Creating
 * the relation must still invalidate (huge c), which depends on (circle radius) through
 * two rules; see InvalidateDependentServices().
 */
void testInvalidateDependentsOfNewRelation(void)
{
	Atom ifactRule = CStringToTerm("circle * radius r");
	Atom bigRule = CStringToClause("big c | ! circle c radius r | ! < r > 10");
	Atom hugeRule = CStringToClause("huge c | ! big c");
	Atom hugeFact = CStringToTerm("huge \"x\"");
	ASSERT_INT32_EQUAL(AssertFormula(ifactRule), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFormula(bigRule), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFormula(hugeRule), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFormula(hugeFact), ASSERT_OK)
	ASSERT_UINT32_EQUAL(countQueryFacts("huge c"), 1)

	// The query creates the relation (circle radius) holding the ifact (circle @1 radius 20)
	ASSERT_UINT32_EQUAL(countQueryFacts("circle c radius 20"), 1)
	ASSERT_UINT32_EQUAL(countQueryFacts("huge c"), 2)

	ASSERT_INT32_EQUAL(RetractFormula(hugeRule), RETRACT_OK)
	ASSERT_INT32_EQUAL(RetractFormula(bigRule), RETRACT_OK)
	ASSERT_INT32_EQUAL(RetractFormula(ifactRule), RETRACT_OK)
	ASSERT_INT32_EQUAL(RetractFormula(hugeFact), RETRACT_OK)
	dropFormRelations("circle 1 radius 1");
	dropFormRelations("huge 1");
	ReleaseFormula(hugeFact);
	ReleaseFormula(hugeRule);
	ReleaseFormula(bigRule);
	ReleaseFormula(ifactRule);
}


/**
 * CLAUDE: The queries (huge c) and (huge c & small d) are compiled before any rule derives
 * (big c), so the queries keep no operator reading (big c). Adding the rule for (big c)
 * must still invalidate both queries; see InvalidateDependentServices().
 */
void testInvalidateDependentsOfNewRule(void)
{
	Atom smallFact = CStringToTerm("small \"y\"");
	Atom hugeFact = CStringToTerm("huge \"x\"");
	Atom hugeRule = CStringToClause("huge c | ! big c");
	Atom bigRule = CStringToClause("big c | ! small c");
	ASSERT_INT32_EQUAL(AssertFormula(smallFact), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFormula(hugeFact), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFormula(hugeRule), ASSERT_OK)
	ASSERT_UINT32_EQUAL(countQueryFacts("huge c"), 1)
	ASSERT_UINT32_EQUAL(countQueryFacts("huge c & small d"), 1)

	ASSERT_INT32_EQUAL(AssertFormula(bigRule), ASSERT_OK)
	ASSERT_UINT32_EQUAL(countQueryFacts("huge c"), 2)
	ASSERT_UINT32_EQUAL(countQueryFacts("huge c & small d"), 2)

	ASSERT_INT32_EQUAL(RetractFormula(bigRule), RETRACT_OK)
	ASSERT_INT32_EQUAL(RetractFormula(hugeRule), RETRACT_OK)
	ASSERT_INT32_EQUAL(RetractFormula(hugeFact), RETRACT_OK)
	ASSERT_INT32_EQUAL(RetractFormula(smallFact), RETRACT_OK)
	dropFormRelations("huge 1");
	dropFormRelations("small 1");
	ReleaseFormula(bigRule);
	ReleaseFormula(hugeRule);
	ReleaseFormula(hugeFact);
	ReleaseFormula(smallFact);
}


/**
 * CLAUDE: With (big c) closed, (! big "z") is inferred while no rule derives (big "z"), and the
 * rule (small c | big c) derives (small "z"). Adding a rule that derives (big "z") must
 * invalidate (small "z"), which reads (! big c) through an INVERT operator.
 */
void testInvalidateDependentsOfClosedRelation(void)
{
	Atom closedBig = CStringToTerm("closed-relation [. big]");
	Atom smallRule = CStringToClause("small c | big c");
	Atom hugeFact = CStringToTerm("huge \"z\"");
	Atom bigRule = CStringToClause("big c | ! huge c");
	ASSERT_INT32_EQUAL(AssertFormula(closedBig), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFormula(smallRule), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFormula(hugeFact), ASSERT_OK)
	ASSERT_UINT32_EQUAL(countQueryFacts("small \"z\""), 1)

	ASSERT_INT32_EQUAL(AssertFormula(bigRule), ASSERT_OK)
	ASSERT_UINT32_EQUAL(countQueryFacts("big \"z\""), 1)
	ASSERT_UINT32_EQUAL(countQueryFacts("small \"z\""), 0)

	ASSERT_INT32_EQUAL(RetractFormula(bigRule), RETRACT_OK)
	ASSERT_INT32_EQUAL(RetractFormula(smallRule), RETRACT_OK)
	ASSERT_INT32_EQUAL(RetractFormula(closedBig), RETRACT_OK)
	ASSERT_INT32_EQUAL(RetractFormula(hugeFact), RETRACT_OK)
	dropFormRelations("huge 1");
	ReleaseFormula(bigRule);
	ReleaseFormula(hugeFact);
	ReleaseFormula(smallRule);
	ReleaseFormula(closedBig);
}


/**
 * CLAUDE: Compiling the conjunction (big c & circle d radius 20) compiles (big c) first, while
 * the relation (circle radius) does not exist. The second term then creates the relation, so
 * the conjunction must be compiled again to read (big @1); see compileQueryVariants().
 */
void testCompileAfterStoreCreated(void)
{
	Atom ifactRule = CStringToTerm("circle * radius r");
	Atom bigRule = CStringToClause("big c | ! circle c radius r | ! < r > 10");
	Atom bigFact = CStringToTerm("big \"x\"");
	ASSERT_INT32_EQUAL(AssertFormula(ifactRule), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFormula(bigRule), ASSERT_OK)
	ASSERT_INT32_EQUAL(AssertFormula(bigFact), ASSERT_OK)

	ASSERT_UINT32_EQUAL(countQueryFacts("big c & circle d radius 20"), 2)
	ASSERT_UINT32_EQUAL(countQueryFacts("big c & circle d radius 20"), 2)

	ASSERT_INT32_EQUAL(RetractFormula(bigRule), RETRACT_OK)
	ASSERT_INT32_EQUAL(RetractFormula(ifactRule), RETRACT_OK)
	ASSERT_INT32_EQUAL(RetractFormula(bigFact), RETRACT_OK)
	dropFormRelations("circle 1 radius 1");
	dropFormRelations("big 1");
	ReleaseFormula(bigFact);
	ReleaseFormula(bigRule);
	ReleaseFormula(ifactRule);
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
	Atom singleTermClause = CreateClause(&term, 1, 0);
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
	TypedRelation listLetter = GetListRelation(AT_LETTER);
	TypedRelation listLength = GetListLengthRelation();
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
	ASSERT_DATA64_EQUAL(ListGetElement(ifact, 1).hash, CreateLetter('A').hash)
	ASSERT_DATA64_EQUAL(ListGetElement(ifact, 2).hash, CreateLetter('B').hash)

	// Creating that list finds the atom already there, and adds no facts
	Atom list = CreateListFromArray(
		(Atom[]) {CreateLetter('A'), CreateLetter('B')}, AT_LETTER, 2);
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
	TypedRelation colourNameRelation = RelationFromFact(FormulaGetView(colourNameTerm));
	ReleaseFormula(colourNameTerm);
	ASSERT_INT32_EQUAL(RelationNRows(colourNameRelation), 1)

	Atom colourCodeTerm = CStringToTerm("colour * code 4");
	TypedRelation colourCodeRelation = RelationFromFact(FormulaGetView(colourCodeTerm));
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
	
	TypedRelation relation = {.form = FormulaGetForm(sameFormTerm), .typeSignature = typeSignature};
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
	TypedRelation listLength = GetListLengthRelation();
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
	TypedRelation listLength = GetListLengthRelation();
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
	TypedRelation relation = {.form = FormulaGetForm(term), .typeSignature = typeSignature};
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
	TypedRelation relation = RelationFromFact(termView);
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
	TypedRelation relation = RelationFromFact(FormulaGetView(term));
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


/* CLAUDE: Assert that the knowledgebase holds the fact written by the C string, then retract it. */
static void retractLoadedFact(char const * factString)
{
	Atom fact = CStringToTerm(factString);
	ASSERT_INT32_EQUAL(AssertFact(FormulaGetView(fact), 0), ASSERT_EXISTED)
	RetractFact(FormulaGetView(fact));
	ReleaseFormula(fact);
}


/* CLAUDE: Drop the relation holding the fact written by the C string. */
static void dropRelationOf(char const * factString)
{
	Atom fact = CStringToTerm(factString);
	DropRelation(RelationFromFact(FormulaGetView(fact)));
	ReleaseFormula(fact);
}


/**
 * CLAUDE: LoadRelationText() asserts one fact per line. Each role of the term form reads
 * the column named by the role in the first line.
 */
void testLoadRelationText(void)
{
	index32 errorIndex;
	Atom parentChild = ParseTermForm("parent child", 0, &errorIndex);
	LoadReport report;

	// CLAUDE: columns separated by spaces, and a blank line that is skipped
	LoadRelationText("parent child\n\"Al\" \"Bob\"\n\n\"Al\" \"Cy\"", parentChild, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_OK)
	ASSERT_UINT32_EQUAL(report.lineNumber, 4)
	ASSERT_UINT32_EQUAL(report.nAsserted, 2)
	ASSERT_UINT32_EQUAL(report.nExisting, 0)

	// CLAUDE: a byte order mark, "\r\n" line endings, spaces around the columns, and an
	// unused column with an empty value. The columns are not in the order of the form.
	LoadRelationText(
		"\xEF\xBB\xBF" "child, age ,parent\r\n\"Bob\", 3 ,\"Al\"\r\n\"Dan\",,\"Al\"\r\n",
		parentChild, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_OK)
	ASSERT_UINT32_EQUAL(report.nAsserted, 1)
	ASSERT_UINT32_EQUAL(report.nExisting, 1)

	// CLAUDE: a line with an empty column read by a role is skipped
	LoadRelationText("parent,child\n\"Al\",\n,\"Eve\"\n", parentChild, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_OK)
	ASSERT_UINT32_EQUAL(report.nAsserted, 0)
	ASSERT_UINT32_EQUAL(report.nSkipped, 2)

	// CLAUDE: columns separated by tabs
	LoadRelationText("parent\tchild\n\"Al\"\t\"Fay\"\n", parentChild, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_OK)
	ASSERT_UINT32_EQUAL(report.nAsserted, 1)

	LoadRelationText("parent,child\n\"Al\"\n", parentChild, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_WRONG_COLUMN_COUNT)
	ASSERT_UINT32_EQUAL(report.lineNumber, 2)
	ASSERT_UINT32_EQUAL(report.nColumns, 1)
	ASSERT_UINT32_EQUAL(report.nHeaderColumns, 2)

	LoadRelationText("parent,child\n\"Al\",x\n", parentChild, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_SYNTAX_ERROR)
	ASSERT_UINT32_EQUAL(report.lineNumber, 2)
	ASSERT_UINT32_EQUAL(report.errorIndex, 5)

	// CLAUDE: a column holds one actor only
	LoadRelationText("parent,child\n\"Al\",\"Bo\" \"Cy\"\n", parentChild, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_SYNTAX_ERROR)
	ASSERT_UINT32_EQUAL(report.errorIndex, 10)

	// CLAUDE: a column of the first line must be a name
	LoadRelationText("parent,1x\n", parentChild, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_SYNTAX_ERROR)
	ASSERT_UINT32_EQUAL(report.lineNumber, 1)
	ASSERT_UINT32_EQUAL(report.errorIndex, 7)

	// CLAUDE: the first line names the columns, and may not be blank
	LoadRelationText("\nparent,child\n", parentChild, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_NO_HEADER)
	ASSERT_UINT32_EQUAL(report.lineNumber, 1)
	LoadRelationText("", parentChild, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_NO_HEADER)

	Atom parentSibling = ParseTermForm("parent sibling", 0, &errorIndex);
	LoadRelationText("parent,child\n", parentSibling, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_MISSING_COLUMN)
	Atom sibling = CreateNameFromCString("sibling");
	ASSERT_TRUE(SameAtoms(report.missingRole, sibling))
	NameRelease(sibling);
	IFactRelease(parentSibling);

	Atom notParentChild = ParseTermForm("! parent child", 0, &errorIndex);
	LoadRelationText("parent,child\n\"Al\",\"Bob\"\n", notParentChild, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_ASSERT_FAILED)
	ASSERT_INT32_EQUAL(report.assertResult, ASSERT_CONTRADICTION)
	IFactRelease(notParentChild);

	retractLoadedFact("parent \"Al\" child \"Bob\"");
	retractLoadedFact("parent \"Al\" child \"Cy\"");
	retractLoadedFact("parent \"Al\" child \"Dan\"");
	retractLoadedFact("parent \"Al\" child \"Fay\"");
	dropRelationOf("parent \"Al\" child \"Bob\"");
	IFactRelease(parentChild);
}


/**
 * CLAUDE: A role name written twice in the term form reads two columns of that name.
 */
void testLoadRepeatedRole(void)
{
	index32 errorIndex;
	Atom spouseSpouse = ParseTermForm("spouse spouse", 0, &errorIndex);
	LoadReport report;

	LoadRelationText("spouse,age,spouse\n\"Al\",3,\"Bea\"\n", spouseSpouse, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_OK)
	ASSERT_UINT32_EQUAL(report.nAsserted, 1)

	LoadRelationText("spouse,age\n", spouseSpouse, &report);
	ASSERT_INT32_EQUAL(report.result, LOAD_MISSING_COLUMN)
	Atom spouse = CreateNameFromCString("spouse");
	ASSERT_TRUE(SameAtoms(report.missingRole, spouse))
	NameRelease(spouse);

	retractLoadedFact("spouse \"Al\" spouse \"Bea\"");
	dropRelationOf("spouse \"Al\" spouse \"Bea\"");
	IFactRelease(spouseSpouse);
}


int main(int argc, char * argv[])
{
	KernelInitialize(TRANSIENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testAssertRetract);
	ExecuteTest(testAssertOverlapsService);
	ExecuteTest(testAssertContradictsStoredFact);
	ExecuteTest(testAssertContradictsDerivedFact);
	ExecuteTest(testAssertClosedRelation);
	ExecuteTest(testAssertClosedComputedRelation);
	ExecuteTest(testAssertClosedRelationRejected);
	ExecuteTest(testAssertFormulaFact);
	ExecuteTest(testAssertFormulaRule);
	ExecuteTest(testAssertFormulaIFactRule);
	ExecuteTest(testAssertFormulaRejects);
	ExecuteTest(testRetractFormulaRule);
	ExecuteTest(testRetractFormulaIFactRule);
	ExecuteTest(testRetractFormulaRejects);
	ExecuteTest(testAssertFactAfterRule);
	ExecuteTest(testInvalidateDependentsOfNewRelation);
	ExecuteTest(testInvalidateDependentsOfNewRule);
	ExecuteTest(testInvalidateDependentsOfClosedRelation);
	ExecuteTest(testCompileAfterStoreCreated);
	ExecuteTest(testCreateIFactTerm);
	ExecuteTest(testCreateIFactIdColumnNotFirst);
	ExecuteTest(testCreateIFactClause);
	ExecuteTest(testCreateIFactList);
	ExecuteTest(testCreateIFactNewRelations);
	ExecuteTest(testCreateIFactTwoIdColumns);
	ExecuteTest(testCreateIFactExisting);
	ExecuteTest(testCreateIFactDefiningFactsProtected);
	ExecuteTest(testCreateIFactRejects);
	ExecuteTest(testLoadRelationText);
	ExecuteTest(testLoadRepeatedRole);
	ExecuteTest(testRetractMathRule);

	UnloadLibraries();
	KernelShutdown();

	return TestSummary();
}
