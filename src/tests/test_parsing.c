
#include "kernel/float.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/Parameter.h"
#include "lang/ClauseForm.h"
#include "lang/ConjunctionForm.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "lang/Variable.h"
#include "library/library.h"
#include "library/string.h"
#include "parser/ClauseBuilder.h"
#include "parser/ConjunctionBuilder.h"
#include "parser/FormTupleParsers.h"
#include "parser/FormulaBuilder.h"
#include "parser/PredicateBuilder.h"
#include "parser/PartBuilder.h"
#include "parser/TermBuilder.h"
#include "parser/Tokenizer.h"
#include "testing/testing.h"


// a set of token pairs for role names and actors

#define EXAMPLE_N_PARTS	3

typedef struct {
	Token nameTokens[EXAMPLE_N_PARTS];
	Token actorTokens[EXAMPLE_N_PARTS];
	Atom names[EXAMPLE_N_PARTS];
	TypedAtom actors[EXAMPLE_N_PARTS];
} TokensFixture;


static void setupTokensFixture(TokensFixture * fixture)
{
	fixture->nameTokens[0] = (Token) {
		TOKEN_NAME,
		CreateTypedAtom(AT_NAME, CreateNameFromCString("foo"))
	};
	fixture->nameTokens[1] = (Token) {
		TOKEN_NAME,
		CreateTypedAtom(AT_NAME, CreateNameFromCString("bar"))
	};
	fixture->nameTokens[2] = (Token) {
		TOKEN_NAME,
		CreateTypedAtom(AT_NAME, CreateNameFromCString("bax"))
	};

	fixture->actorTokens[0] = (Token) {
		.type = TOKEN_VARIABLE,
		.typedAtom = CreateTypedAtom(AT_VARIABLE, CreateVariable('x'))
	};	
	fixture->actorTokens[1] = (Token) {
		.type = TOKEN_NUMBER,
		.typedAtom = CreateTypedAtom(AT_FLOAT, (Atom) {._float = 123.45})
	};
	fixture->actorTokens[2] = (Token) {
		.type = TOKEN_STRING,
		.typedAtom = CreateTypedAtom(AT_ID, CreateStringFromCString("foobar"))
	};

	for(index8 i = 0; i < EXAMPLE_N_PARTS; i++) {
		fixture->names[i] = fixture->nameTokens[i].typedAtom.atom;
		fixture->actors[i] = fixture->actorTokens[i].typedAtom;
	}
}


static void teardownTokensFixture(TokensFixture * fixture)
{
	for(index8 i = 0; i < EXAMPLE_N_PARTS; i++) {
		ReleaseTypedAtom(fixture->nameTokens[i].typedAtom);
		ReleaseTypedAtom(fixture->actorTokens[i].typedAtom);
	}
}


static void testPartBuilder(void)
{
	TokensFixture fixture;
	setupTokensFixture(&fixture);

	PartBuilder builder;
	InitializePartBuilder(&builder, FORMULA_TOP_SCOPE);
	ASSERT_FALSE(PartBuilderComplete(&builder))
	for(index8 i = 0; i < EXAMPLE_N_PARTS; i++) {
		ASSERT_TRUE(PartBuilderPush(&builder, fixture.nameTokens[i]))
		ASSERT_FALSE(PartBuilderComplete(&builder))
		
		ASSERT_TRUE(PartBuilderPush(&builder, fixture.actorTokens[i]))
		ASSERT_TRUE(PartBuilderComplete(&builder))

		ASSERT_DATA64_EQUAL(PartBuilderGetRole(&builder).hash, fixture.names[i].hash)

		TypedAtom actor = PartBuilderGetActor(&builder);
		ASSERT_TRUE(SameTypedAtoms(actor, fixture.actors[i]))

		PartBuilderReset(&builder);
		ASSERT_TRUE(PartBuilderIsEmpty(&builder))
	}

	teardownTokensFixture(&fixture);
}

#define EXAMPLE_PREDICATE_ARITY 	(EXAMPLE_N_PARTS)

typedef struct {
	TokensFixture tokensFixture;
	Atom predicate;
} PredicateFixture;


// crete a predicate from parts fixture
static void setupPredicateFixture(PredicateFixture * fixture)
{
	setupTokensFixture(&(fixture->tokensFixture));
	// the predicate (bar 123.450000 baz "foobar" foo x)
	fixture->predicate = CreatePredicate(
		fixture->tokensFixture.names,
		fixture->tokensFixture.actors,
		EXAMPLE_N_PARTS
	);
}

static void teardownPredicateFixture(PredicateFixture * fixture)
{
	ReleaseFormula(fixture->predicate);
	teardownTokensFixture(&(fixture->tokensFixture));
}


static void testPredicateBuilder(void)
{
	PredicateFixture fixture;
	setupPredicateFixture(&fixture);

	TokensFixture * tokensFixture = &(fixture.tokensFixture);

	PredicateBuilder builder;
	InitializePredicateBuilder(&builder, FORMULA_TOP_SCOPE);
	ASSERT_FALSE(PredicateBuilderIsValid(&builder));

	for(index8 i = 0; i < EXAMPLE_PREDICATE_ARITY; i++) {
		ASSERT_TRUE(PredicateBuilderPush(&builder, tokensFixture->nameTokens[i]))
		ASSERT_FALSE(PredicateBuilderIsValid(&builder))
		ASSERT_TRUE(PredicateBuilderPush(&builder, tokensFixture->actorTokens[i]))
		ASSERT_TRUE(PredicateBuilderIsValid(&builder))
	}
	Atom predicate = PredicateBuilderCreateFormula(&builder);

	ASSERT_TRUE(SameAtoms(predicate, fixture.predicate))

	ReleaseFormula(predicate);
	CleanupPredicateBuilder(&builder);

	teardownPredicateFixture(&fixture);
}


typedef struct {
	PredicateFixture predicateFixture;
	Atom term;
	Atom negatedTerm;
} TermFixture;


static void setupTermFixture(TermFixture * fixture)
{
	setupPredicateFixture(&(fixture->predicateFixture));
	fixture->term = CreateTerm(fixture->predicateFixture.predicate, true);
	fixture->negatedTerm = CreateTerm(fixture->predicateFixture.predicate, false);
}

static void teardownTermFixture(TermFixture * fixture)
{
	ReleaseFormula(fixture->term);
	ReleaseFormula(fixture->negatedTerm);
	teardownPredicateFixture(&(fixture->predicateFixture));
}

static void testTermBuilder(void)
{
	TermFixture fixture;
	setupTermFixture(&fixture);

	TokensFixture * tokensFixture = &(fixture.predicateFixture.tokensFixture);

	TermBuilder builder;
	InitializeTermBuilder(&builder, FORMULA_TOP_SCOPE);
	// test with and without negation
	for(index8 k = 0; k <= 1; k++) {
		ASSERT_FALSE(TermBuilderIsValid(&builder))
		bool sign = (bool) k;
		if(!sign) {
			// negated predicate for k == 0
			ASSERT_TRUE(TermBuilderPush(&builder, (Token) {TOKEN_NOT, invalidAtom}))
			ASSERT_FALSE(TermBuilderIsValid(&builder))
		}
		for(index8 i = 0; i < EXAMPLE_N_PARTS; i++) {
			ASSERT_TRUE(TermBuilderPush(&builder, tokensFixture->nameTokens[i]))
			ASSERT_FALSE(TermBuilderIsValid(&builder))
			ASSERT_TRUE(TermBuilderPush(&builder, tokensFixture->actorTokens[i]))
			ASSERT_TRUE(TermBuilderIsValid(&builder))
		}
		Atom term = TermBuilderCreateFormula(&builder);

		Atom fixtureTerm = sign ? fixture.term : fixture.negatedTerm;
		ASSERT_TRUE(SameAtoms(term, fixtureTerm))

		ReleaseFormula(term);
		TermBuilderReset(&builder);
	}
	TermBuilderFree(&builder);

	teardownTermFixture(&fixture);
}


#define EXAMPLE_CLAUSE_N_TERMS		2
#define EXAMPLE_CLAUSE_ARITY		(EXAMPLE_CLAUSE_N_TERMS * EXAMPLE_PREDICATE_ARITY)
#define EXAMPLE_CLAUSE_N_TOKENS		2 * EXAMPLE_CLAUSE_ARITY + (EXAMPLE_CLAUSE_N_TERMS - 1)


typedef struct {
	TermFixture termFixture;
	Atom terms[EXAMPLE_CLAUSE_N_TERMS];
	Atom clause;
} ClauseFixture;


void setupClauseFixture(ClauseFixture * fixture)
{
	setupTermFixture(&(fixture->termFixture));
	fixture->terms[0] = fixture->termFixture.negatedTerm;
	fixture->terms[1] = fixture->termFixture.term;
	fixture->clause = CreateClause(fixture->terms, EXAMPLE_CLAUSE_N_TERMS);
}

static void teardownClauseFixture(ClauseFixture * fixture)
{
	teardownTermFixture(&(fixture->termFixture));
	ReleaseFormula(fixture->clause);
}

static void testClauseBuilder(void)
{
	ClauseFixture fixture;
	setupClauseFixture(&fixture);

	TokensFixture * tokensFixture = 
		&(fixture.termFixture.predicateFixture.tokensFixture);

	ClauseBuilder builder;
	InitializeClauseBuilder(&builder, FORMULA_TOP_SCOPE);
	ASSERT_FALSE(ClauseBuilderIsValid(&builder))
	ASSERT_TRUE(ClauseBuilderIsEmpty(&builder))

	for(index8 i = 0; i < EXAMPLE_CLAUSE_N_TERMS; i++) {
		size8 termArity = FormulaArity(fixture.terms[i]);
		if(i == 0) {
			// negated predicate
			ASSERT_TRUE(ClauseBuilderPush(&builder, (Token) {TOKEN_NOT, invalidAtom}))
			ASSERT_FALSE(ClauseBuilderIsValid(&builder))
		}
		for(index8 j = 0; j < termArity; j++) {
			ASSERT_TRUE(
				ClauseBuilderPush(&builder, tokensFixture->nameTokens[j]))
			ASSERT_FALSE(ClauseBuilderIsValid(&builder))
			ASSERT_TRUE(
				ClauseBuilderPush(&builder, tokensFixture->actorTokens[j]))
			ASSERT_TRUE(ClauseBuilderIsValid(&builder))
		}
		if(i < EXAMPLE_CLAUSE_N_TERMS - 1) {
			ASSERT_TRUE(
				ClauseBuilderPush(&builder, (Token) {TOKEN_OR, invalidAtom}))
			ASSERT_FALSE(ClauseBuilderIsValid(&builder))
		}
		ASSERT_FALSE(ClauseBuilderIsEmpty(&builder))
	}
	ASSERT_TRUE(ClauseBuilderFinish(&builder))
	Atom clause = ClauseBuilderCreateFormula(&builder);
	CleanupClauseBuilder(&builder);

	ASSERT_TRUE(SameAtoms(clause, fixture.clause))

	ReleaseFormula(clause);
	teardownClauseFixture(&fixture);
}


/**
 * A service signature parses as an ordinary term whose actors are parameters.
 * The actors are in canonical role order, which is not the order they are written in,
 * so each parameter is found by its number rather than its position.
 * This is what RegisterMachineService() relies on; see library/MachineService.h
 */
static void testCStringToSignature(void)
{
	Atom signature = CStringToTerm("+ #1<INT + #2<INT = #3>INT");
	ASSERT_UINT32_EQUAL(FormulaGetActors(signature)->nAtoms, 3)

	// every actor is a parameter, and the numbers are a permutation of 1..3
	bool found[3] = {false, false, false};
	for(index8 i = 0; i < 3; i++) {
		TypedAtom actor = TypedTupleGetElement(FormulaGetActors(signature), i);
		ASSERT_UINT32_EQUAL(actor.type, AT_PARAMETER)
		ASSERT_UINT32_EQUAL(actor.atom.parameter.atomType, AT_INT)
		index8 number = actor.atom.parameter.number;
		ASSERT_TRUE((number >= 1) && (number <= 3))
		ASSERT_FALSE(found[number - 1])
		found[number - 1] = true;
		// the two summands are inputs and the sum is the output
		ASSERT_UINT32_EQUAL(
			actor.atom.parameter.io,
			(number == 3) ? PARAMETER_OUT : PARAMETER_IN
		)
	}
	ReleaseFormula(signature);
}


static void testCStringToPredicate(void)
{
	char const * exampleString = "foo 123 baz \"foobar\" bar 456 bar 789";
	Atom predicate = CStringToPredicate(exampleString);

	ASSERT_UINT32_EQUAL(PredicateArity(FormulaGetForm(predicate)), 4)
	ASSERT_UINT32_EQUAL(FormulaGetActors(predicate)->nAtoms, 4)

	Atom baz = CreateNameFromCString("baz");
	index8 bazRoleIndex = PredicateRoleIndex(FormulaGetForm(predicate), baz);
	NameRelease(baz);

	Atom string = CreateStringFromCString("foobar");
	ASSERT_TRUE(
		SameTypedAtoms(
			TypedTupleGetElement(FormulaGetActors(predicate), bazRoleIndex),
			CreateTypedAtom(AT_ID, string)
		)
	)
	IFactRelease(string);

	ReleaseFormula(predicate);
}


static void testCStringToClause(void)
{
	// NOTE: this string must be in canonical order
	Atom clause = CStringToClause("aarf \"foobar\" | foo x bar 123.45");
	// PrintFormula(clause);
	// PrintChar('\n');

	ASSERT_UINT32_EQUAL(ClauseArity(FormulaGetForm(clause)), 3);
	ASSERT_UINT32_EQUAL(FormulaGetActors(clause)->nAtoms, 3)

	Atom string = CreateStringFromCString("foobar");
	ASSERT_TRUE(
		SameTypedAtoms(
			TypedTupleGetElement(FormulaGetActors(clause), 0),
			CreateTypedAtom(AT_ID, string)
		)
	)
	IFactRelease(string);
	ASSERT_TRUE(
		SameTypedAtoms(
			TypedTupleGetElement(FormulaGetActors(clause), 1),
			CreateTypedAtom(AT_VARIABLE, CreateVariable('x'))
		)
	)
	ASSERT_TRUE(
		SameTypedAtoms(
			TypedTupleGetElement(FormulaGetActors(clause), 2),
			CreateTypedAtom(AT_FLOAT, (Atom) {._float = 123.45})
		)
	)

	ReleaseFormula(clause);
}


static void testCStringToConjunction(void)
{
	// NOTE: this string must be in canonical order
	Atom conjunction = CStringToConjunction("aarf \"foobar\" & foo x bar 123.45 & barf 42 frob y");
	// PrintFormula(conjunction);
	// PrintChar('\n');

	ASSERT_UINT32_EQUAL(ConjunctionFormArity(FormulaGetForm(conjunction)), 5)
	ASSERT_UINT32_EQUAL(FormulaGetActors(conjunction)->nAtoms, 5)

	ReleaseFormula(conjunction);
}


static void testCStringToFormula(void)
{
	// a formula with neither | nor & is a term
	Atom term = CStringToFormula("foo x bar 123.45");
	ASSERT_TRUE(FormulaIsTerm(term))
	Atom expectedTerm = CStringToTerm("foo x bar 123.45");
	ASSERT_TRUE(SameAtoms(term, expectedTerm))
	ReleaseFormula(expectedTerm);
	ReleaseFormula(term);

	// a negated term is still a term
	Atom negatedTerm = CStringToFormula("! foo 42");
	ASSERT_TRUE(FormulaIsTerm(negatedTerm))
	ASSERT_FALSE(TermFormGetSign(FormulaGetForm(negatedTerm)))
	ReleaseFormula(negatedTerm);

	// a formula with | but no & is a clause
	// NOTE: this string must be in canonical order
	Atom clause = CStringToFormula("aarf \"foobar\" | foo x bar 123.45");
	ASSERT_TRUE(FormulaIsClause(clause))
	Atom expectedClause = CStringToClause("aarf \"foobar\" | foo x bar 123.45");
	ASSERT_TRUE(SameAtoms(clause, expectedClause))
	ReleaseFormula(expectedClause);
	ReleaseFormula(clause);

	// a formula with & is a conjunction
	Atom conjunction = CStringToFormula("aarf \"foobar\" & foo x bar 123.45 & barf 42 frob y");
	ASSERT_TRUE(FormulaIsConjunction(conjunction))
	Atom expectedConjunction = CStringToConjunction("aarf \"foobar\" & foo x bar 123.45 & barf 42 frob y");
	ASSERT_TRUE(SameAtoms(conjunction, expectedConjunction))
	ReleaseFormula(expectedConjunction);
	ReleaseFormula(conjunction);
}


/**
 * Build the term (<reflectionRole> [<formula>] <numberRole> <number>),
 * with the given formula as the actor of the reflection role.
 * reflectionType is AT_FORMULA for [<formula>], or AT_RELATION for [[<formula>]]
 */
static Atom createTermWithReflection(
	char const * reflectionRole, byte reflectionType, Atom reflectedFormula,
	char const * numberRole, int64 number)
{
	Atom roles[2] = {
		CreateNameFromCString(reflectionRole),
		CreateNameFromCString(numberRole)
	};
	TypedAtom actors[2] = {
		CreateTypedAtom(reflectionType, reflectedFormula),
		CreateTypedAtom(AT_INT, (Atom) {._int = number})
	};
	Atom predicate = CreatePredicate(roles, actors, 2);
	Atom term = CreateTerm(predicate, true);

	ReleaseFormula(predicate);
	NameRelease(roles[0]);
	NameRelease(roles[1]);
	return term;
}


/**
 * A TokenHandler function using a FormulaBuilder, for use with TokenizeCString();
 * see TermBuilderTokenHandler() for the same over a TermBuilder.
 */
static bool formulaBuilderTokenHandler(void * context, Token token)
{
	return FormulaBuilderPush((FormulaBuilder *) context, token);
}


/* CLAUDE: Same as testReflection(), for either reflection syntax. A reflectionType
   AT_FORMULA parses "term [<formulaString>] arity 2", and a reflectionType AT_RELATION parses
   "term [[<formulaString>]] arity 2". */
static void testReflectionOfType(char const * formulaString, byte reflectionType)
{
	// Create the expected formula, parsing only the formula inside the reflection.
	// This avoids running the same code path being tested.
	FormulaBuilder formulaBuilder;
	InitializeFormulaBuilder(&formulaBuilder, FORMULA_REFLECTED_SCOPE);
	TokenizeCString(formulaString, formulaBuilderTokenHandler, &formulaBuilder);
	ASSERT(FormulaBuilderFinish(&formulaBuilder))
	Atom reflectedFormula = FormulaBuilderCreateFormula(&formulaBuilder);
	CleanupFormulaBuilder(&formulaBuilder);
	Atom expectedTerm = createTermWithReflection("term", reflectionType, reflectedFormula, "arity", 2);

	// Parse the corresponding syntax string
	TermBuilder builder;
	InitializeTermBuilder(&builder, FORMULA_TOP_SCOPE);
	bool isRelation = (reflectionType == AT_RELATION);
	TokenizeCString(isRelation ? "term [[" : "term [", TermBuilderTokenHandler, &builder);
	TokenizeCString(formulaString, TermBuilderTokenHandler, &builder);
	TokenizeCString(isRelation ? "]] arity 2" : "] arity 2", TermBuilderTokenHandler, &builder);
	ASSERT(TermBuilderIsValid(&builder))
	Atom parsedTerm = TermBuilderCreateFormula(&builder);
	TermBuilderFree(&builder);

	ASSERT_TRUE(FormulaIsTerm(parsedTerm))
	ASSERT_TRUE(SameAtoms(parsedTerm, expectedTerm))

	ReleaseFormula(parsedTerm);
	ReleaseFormula(expectedTerm);
	ReleaseFormula(reflectedFormula);
}


/**
 * Parse the string "term [<formulaString>] arity 2" where the given
 * formulaString is inserted, and compare it against the term
 * (term <reflectedFormula>) arity 2) constructed independently.
 * This tests whether the reflection [] syntax is working correctly.
 */
static void testReflection(char const * formulaString)
{
	testReflectionOfType(formulaString, AT_FORMULA);
}


/**
 * A letter is an actor, written 'A. This is the syntax PrintLetter() prints, so a formula
 * holding a letter reads back as the formula it was printed from.
 */
static void testLetterActor(void)
{
	Atom term = CStringToTerm("list \"ab\" position 1 element 'A");
	TypedTuple const * actors = FormulaGetActors(term);
	ASSERT_UINT32_EQUAL(actors->nAtoms, 3)

	Atom elementRole = CreateNameFromCString("element");
	index8 elementIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(FormulaGetForm(term)), elementRole);
	NameRelease(elementRole);

	TypedAtom element = TypedTupleGetElement(actors, elementIndex);
	ASSERT_UINT32_EQUAL(element.type, AT_LETTER)
	ASSERT_TRUE(SameAtoms(element.atom, GetAlphabetLetter('A')))

	// a letter is case-insensitive, so the same term is written either way
	Atom lowerTerm = CStringToTerm("list \"ab\" position 1 element 'a");
	ASSERT_TRUE(SameAtoms(term, lowerTerm))
	ReleaseFormula(lowerTerm);
	ReleaseFormula(term);
}


/**
 * Test parsing a term containing a generator (*) actor
 */
static void testGeneratorActor(void)
{
	Atom term = CStringToTerm("list \"abc\" position 1 element *");
	TypedTuple const * actors = FormulaGetActors(term);
	ASSERT_UINT32_EQUAL(actors->nAtoms, 3)

	Atom elementRole = CreateNameFromCString("element");
	index8 elementIndex = PredicateRoleIndex(
		TermFormGetPredicateForm(FormulaGetForm(term)), elementRole);
	NameRelease(elementRole);

	TypedAtom element = TypedTupleGetElement(actors, elementIndex);
	ASSERT_TRUE(SameTypedAtoms(element, generatorAtom))
	ReleaseFormula(term);
}


/**
 * ParseFormula() reads what CStringToFormula() reads, but reports invalid syntax
 * instead of aborting on it, naming the character where the string went wrong.
 */
static void testParseFormula(void)
{
	index32 errorPosition;

	// a valid string parses to the formula CStringToFormula() yields for it
	Atom formula = ParseFormula("foo x bar 123.45", &errorPosition);
	ASSERT_TRUE(formula.hash != 0)
	Atom expectedFormula = CStringToFormula("foo x bar 123.45");
	ASSERT_TRUE(SameAtoms(formula, expectedFormula))
	ReleaseFormula(expectedFormula);
	ReleaseFormula(formula);

	// a character belonging to no token is reported where it stands
	ASSERT_UINT64_EQUAL(ParseFormula("foo x bar %", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 10)

	// a number stands where an actor does and not where a role name does, so it is
	// reported at its first character rather than at the whitespace before it
	ASSERT_UINT64_EQUAL(ParseFormula("foo x 42 bar 1", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 6)

	// a variable is named by a single letter, so a word too long to be one is reported
	// at the letter that makes it too long; see enum TokenizerState
	ASSERT_UINT64_EQUAL(ParseFormula("foo xy bar 1", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 5)

	// a string ending in the middle of a formula is reported at its end
	ASSERT_UINT64_EQUAL(ParseFormula("foo x bar", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 9)

	// an unterminated string is read to the end of the line
	ASSERT_UINT64_EQUAL(ParseFormula("foo \"abc", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 8)

	// a string is reported at its first character that is not a letter
	ASSERT_UINT64_EQUAL(ParseFormula("foo \"ab1\"", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 7)

	// an empty string is reported at its closing quote
	ASSERT_UINT64_EQUAL(ParseFormula("foo \"\"", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 5)

	// an unterminated reflection abandons its nested builder
	ASSERT_UINT64_EQUAL(ParseFormula("foo [ bar 1", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 11)

	// a parameter naming no known atom type is rejected, not asserted on
	ASSERT_UINT64_EQUAL(ParseFormula("foo #1<NOTATYPE", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 15)

	// a string holding no formula at all
	ASSERT_UINT64_EQUAL(ParseFormula("", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 0)

	// A quoted variable is rejected outside of a reflections
	ASSERT_UINT64_EQUAL(ParseFormula("foo ^x bar 42", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 4)

	// CLAUDE: an AT_ID atom written by its hash is the actor of the formula
	Atom string = CreateStringFromCString("abc");
	char formulaString[32];
	FormatString(formulaString, sizeof(formulaString), "foo @%016llx", (unsigned long long) string.hash);
	formula = ParseFormula(formulaString, &errorPosition);
	ASSERT_TRUE(formula.hash != 0)
	TypedAtom actor = TypedTupleGetElement(FormulaGetView(formula).actors, 0);
	ASSERT_UINT32_EQUAL(actor.type, AT_ID)
	ASSERT_UINT64_EQUAL(actor.atom.hash, string.hash)
	ReleaseFormula(formula);
	IFactRelease(string);

	// CLAUDE: a hash naming no stored AT_ID atom is reported where the hash ends
	ASSERT_UINT64_EQUAL(ParseFormula("foo @0000000000000000", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 21)
}


/**
 * CLAUDE: ParseTermForm() yields the form of a term with the same role names,
 * and the order of the role names in the form.
 */
static void testParseTermForm(void)
{
	index8 roleOrder[RELATION_MAX_ARITY];
	index32 errorIndex;

	Atom form = ParseTermForm("parent child", roleOrder, &errorIndex);
	Atom term = CStringToTerm("parent 1 child 2");
	ASSERT_TRUE(SameAtoms(form, FormulaGetForm(term)))
	Atom roleNames[2] = {CreateNameFromCString("parent"), CreateNameFromCString("child")};
	Atom predicateForm = TermFormGetPredicateForm(form);
	for(index8 i = 0; i < 2; i++)
		ASSERT_UINT32_EQUAL(PredicateRoleIndex(predicateForm, roleNames[roleOrder[i]]), i)
	NameRelease(roleNames[0]);
	NameRelease(roleNames[1]);
	ReleaseFormula(term);
	IFactRelease(form);

	form = ParseTermForm("! parent child", roleOrder, &errorIndex);
	term = CStringToTerm("! parent 1 child 2");
	ASSERT_TRUE(SameAtoms(form, FormulaGetForm(term)))
	ReleaseFormula(term);
	IFactRelease(form);

	form = ParseTermForm("+ + =", roleOrder, &errorIndex);
	term = CStringToTerm("+ 1 + 2 = 3");
	ASSERT_TRUE(SameAtoms(form, FormulaGetForm(term)))
	ReleaseFormula(term);
	IFactRelease(form);

	ASSERT_UINT64_EQUAL(ParseTermForm("", roleOrder, &errorIndex).hash, 0)
	ASSERT_UINT32_EQUAL(errorIndex, 0)
	ASSERT_UINT64_EQUAL(ParseTermForm("!", roleOrder, &errorIndex).hash, 0)
	ASSERT_UINT32_EQUAL(errorIndex, 1)
	ASSERT_UINT64_EQUAL(ParseTermForm("foo 1", roleOrder, &errorIndex).hash, 0)
	ASSERT_UINT32_EQUAL(errorIndex, 4)
	ASSERT_UINT64_EQUAL(ParseTermForm("foo ! bar", roleOrder, &errorIndex).hash, 0)
	ASSERT_UINT32_EQUAL(errorIndex, 4)
	ASSERT_UINT64_EQUAL(ParseTermForm("foo \"bar\"", roleOrder, &errorIndex).hash, 0)
	ASSERT_UINT32_EQUAL(errorIndex, 4)
	// CLAUDE: one role name more than RELATION_MAX_ARITY
	ASSERT_UINT64_EQUAL(ParseTermForm("a b c d e f g h i", roleOrder, &errorIndex).hash, 0)
	ASSERT_UINT32_EQUAL(errorIndex, 16)
}


/**
 * CLAUDE: ParseActors() reads constant actors only, up to the given number of actors.
 */
static void testParseActors(void)
{
	TypedAtom actors[4];
	size8 nActors;
	index32 errorIndex;

	ASSERT_TRUE(ParseActors("\"abc\" 42 'A 1.5", actors, 4, &nActors, &errorIndex))
	ASSERT_UINT32_EQUAL(nActors, 4)
	ASSERT_UINT32_EQUAL(actors[0].type, AT_ID)
	ASSERT_UINT32_EQUAL(actors[1].type, AT_INT)
	ASSERT_INT64_EQUAL(actors[1].atom._int, 42)
	ASSERT_UINT32_EQUAL(actors[2].type, AT_LETTER)
	ASSERT_UINT32_EQUAL(actors[3].type, AT_FLOAT)
	for(index8 i = 0; i < nActors; i++)
		ReleaseTypedAtom(actors[i]);

	ASSERT_TRUE(ParseActors("", actors, 4, &nActors, &errorIndex))
	ASSERT_UINT32_EQUAL(nActors, 0)

	// CLAUDE: a fact has no variables
	ASSERT_FALSE(ParseActors("\"abc\" x", actors, 4, &nActors, &errorIndex))
	ASSERT_UINT32_EQUAL(errorIndex, 6)
	ASSERT_FALSE(ParseActors("1 2 3", actors, 2, &nActors, &errorIndex))
	ASSERT_UINT32_EQUAL(errorIndex, 4)
	ASSERT_FALSE(ParseActors("\"ab1\"", actors, 4, &nActors, &errorIndex))
	ASSERT_UINT32_EQUAL(errorIndex, 3)
	// CLAUDE: a letter must be followed by a separator; see enum TokenizerInputMode
	ASSERT_FALSE(ParseActors("'A1", actors, 4, &nActors, &errorIndex))
	ASSERT_UINT32_EQUAL(errorIndex, 2)
}


/**
 * A clause states each of its terms once, and a conjunction each of its clauses once,
 * so a formula repeating one of them is not a formula. Repeating a term *form* is fine:
 * two terms of one form differ by their actors.
 */
static void testRepeatedTermRejected(void)
{
	index32 errorPosition;

	// A repeat with more formula after it is reported at the separator that completed it
	ASSERT_UINT64_EQUAL(ParseFormula("foo 1 | foo 1 | bar 2", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 14)
	ASSERT_UINT64_EQUAL(ParseFormula("foo 1 & foo 1 & baz 3", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 14)

	// CLAUDE: Mixing | and & is not a formula; the second connective is rejected where it appears
	ASSERT_UINT64_EQUAL(ParseFormula("foo 1 | foo 1 & bar 2", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 14)

	// The last term of a formula is only completed once the string has ended,
	// so a repeat there is reported at the end
	ASSERT_UINT64_EQUAL(ParseFormula("foo 1 | foo 1", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 13)
	ASSERT_UINT64_EQUAL(ParseFormula("foo 1 bar 2 & foo 1 bar 2", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 25)

	// A reflection holds a formula, and is rejected at its closing bracket
	ASSERT_UINT64_EQUAL(ParseFormula("foo [ bar 1 | bar 1 ]", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 20)

	// Two terms of one term form are distinct terms, and so are two such clauses
	Atom clause = ParseFormula("foo 1 | foo 2", &errorPosition);
	ASSERT_TRUE(clause.hash != 0)
	ASSERT_UINT32_EQUAL(ClauseFormNTerms(FormulaGetForm(clause)), 2)
	ASSERT_UINT32_EQUAL(ClauseFormNTermForms(FormulaGetForm(clause)), 1)
	ReleaseFormula(clause);

	Atom conjunction = ParseFormula("foo 1 & foo 2", &errorPosition);
	ASSERT_TRUE(conjunction.hash != 0)
	ASSERT_UINT32_EQUAL(ConjunctionFormNTermsTotal(FormulaGetForm(conjunction)), 2)
	ASSERT_UINT32_EQUAL(ConjunctionFormNUniqueTermForms(FormulaGetForm(conjunction)), 1)
	ReleaseFormula(conjunction);
}


/**
 * Test parsing a term containing a reflection [foo "a" bar b]
 */
static void testReflectedTerm(void)
{
	testReflection("foo \"a\" bar b");
}


static void testReflectedNegatedTerm(void)
{
	testReflection("! foo 42");
}

/**
 * Test parsing a term containing a reflection with a quoted variable ^b
 */
static void testReflectedTermWithQuote(void)
{
	testReflection("foo \"a\" bar ^b");
}


static void testReflectedClause(void)
{
	testReflection("foo 1 | bar 2");
}


static void testReflectedConjunction(void)
{
	testReflection("foo 1 & bar 2");
}

/**
 * Test parting a formula with nested reflections
 * term [foo [bar 1] baz 2] arity 3
 */
static void testNestedReflection(void)
{
	// the expected term is built from the inside out, so that it does not
	// depend on the reflection parsing being tested
	Atom innermost = CStringToTerm("bar 1");
	Atom inner = createTermWithReflection("foo", AT_FORMULA, innermost, "baz", 2);
	Atom expectedTerm = createTermWithReflection("term", AT_FORMULA, inner, "arity", 3);

	Atom parsed = CStringToTerm("term [foo [bar 1] baz 2] arity 3");
	ASSERT_TRUE(SameAtoms(parsed, expectedTerm))

	ReleaseFormula(parsed);
	ReleaseFormula(expectedTerm);
	ReleaseFormula(inner);
	ReleaseFormula(innermost);
}


static void testReflectionRejected(void)
{
	Token nameToken = (Token) {
		TOKEN_NAME,
		CreateTypedAtom(AT_NAME, CreateNameFromCString("foo"))
	};
	Token beginToken = (Token) {TOKEN_BEGIN_REFLECT, invalidAtom};
	Token endToken = (Token) {TOKEN_END_REFLECT, invalidAtom};

	PartBuilder builder;
	InitializePartBuilder(&builder, FORMULA_TOP_SCOPE);

	// a reflection cannot stand where a role name is expected
	ASSERT_FALSE(PartBuilderPush(&builder, beginToken))

	// a reflection holding no formula is not an actor
	ASSERT_TRUE(PartBuilderPush(&builder, nameToken))
	ASSERT_TRUE(PartBuilderPush(&builder, beginToken))
	ASSERT_FALSE(PartBuilderPush(&builder, endToken))
	ASSERT_FALSE(PartBuilderComplete(&builder))

	// resetting an unterminated reflection releases its nested builder
	PartBuilderReset(&builder);
	ASSERT_TRUE(PartBuilderIsEmpty(&builder))

	ReleaseTypedAtom(nameToken.typedAtom);
}


/* CLAUDE: A relation [[ ... ]] holds any formula a reflection [ ... ] holds. */
static void testReflectedRelation(void)
{
	testReflectionOfType("foo \"a\" bar b", AT_RELATION);
	testReflectionOfType("! foo 42", AT_RELATION);
	testReflectionOfType("foo \"a\" bar ^b", AT_RELATION);
	testReflectionOfType("foo 1 | bar 2", AT_RELATION);
	testReflectionOfType("foo 1 & bar 2", AT_RELATION);
}


/* CLAUDE: Parse a term with a reflection inside a reflection, where the outer reflection
   has type outerType and the inner reflection has type innerType. Compare the parsed term
   with the term built by createTermWithReflection(). */
static void testNestedReflectionOfType(char const * termString, byte outerType, byte innerType)
{
	Atom innermost = CStringToTerm("bar 1");
	Atom inner = createTermWithReflection("foo", innerType, innermost, "baz", 2);
	Atom expectedTerm = createTermWithReflection("term", outerType, inner, "arity", 3);

	Atom parsed = CStringToTerm(termString);
	ASSERT_TRUE(SameAtoms(parsed, expectedTerm))

	ReleaseFormula(parsed);
	ReleaseFormula(expectedTerm);
	ReleaseFormula(inner);
	ReleaseFormula(innermost);
}


/* CLAUDE: A relation nests within a formula reflection, and the other way around. Three
   closing brackets in a row close one reflection and one relation. */
static void testNestedRelation(void)
{
	testNestedReflectionOfType("term [[foo [bar 1] baz 2]] arity 3", AT_RELATION, AT_FORMULA);
	testNestedReflectionOfType("term [foo [[bar 1]] baz 2] arity 3", AT_FORMULA, AT_RELATION);
	testNestedReflectionOfType("term [[foo [[bar 1]] baz 2]] arity 3", AT_RELATION, AT_RELATION);
	testNestedReflectionOfType("term [[baz 2 foo [bar 1]]] arity 3", AT_RELATION, AT_FORMULA);
}


/* CLAUDE: Invalid relation syntax, reported by ParseFormula() at the offending token. */
static void testRelationRejected(void)
{
	index32 errorPosition;

	// a relation is closed by two brackets, so a name after one bracket is rejected
	ASSERT_UINT64_EQUAL(ParseFormula("foo [[bar 1] baz 2", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 13)

	// a relation missing the second closing bracket is reported at the end of the string
	ASSERT_UINT64_EQUAL(ParseFormula("foo [[bar 1]", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 12)

	// a relation holding no formula
	ASSERT_UINT64_EQUAL(ParseFormula("foo [[]]", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 6)

	// a third opening bracket stands where a role name is expected
	ASSERT_UINT64_EQUAL(ParseFormula("foo [[[bar 1]]]", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 6)

	// an opening bracket after an actor stands where a role name is expected
	ASSERT_UINT64_EQUAL(ParseFormula("foo [bar 1 [baz 2]]", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 11)
	ASSERT_UINT64_EQUAL(ParseFormula("foo 1 [bar 2]", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 6)

	// whitespace between the brackets is allowed; see PartBuilder
	Atom spaced = ParseFormula("foo [ [bar 1] ]", &errorPosition);
	Atom expected = CStringToFormula("foo [[bar 1]]");
	ASSERT_TRUE(SameAtoms(spaced, expected))
	ASSERT_UINT32_EQUAL(FormulaGetActors(spaced)->nAtoms, 1)
	ASSERT_UINT32_EQUAL(TypedTupleGetElement(FormulaGetActors(spaced), 0).type, AT_RELATION)
	ReleaseFormula(spaced);
	ReleaseFormula(expected);
}


/* CLAUDE: Resetting a part builder releases a relation at each stage of parsing the relation. */
static void testRelationPartBuilderReset(void)
{
	Token nameToken = (Token) {
		TOKEN_NAME,
		CreateTypedAtom(AT_NAME, CreateNameFromCString("foo"))
	};
	Token innerNameToken = (Token) {
		TOKEN_NAME,
		CreateTypedAtom(AT_NAME, CreateNameFromCString("bar"))
	};
	Token numberToken = (Token) {TOKEN_NUMBER, CreateTypedAtom(AT_INT, (Atom) {._int = 1})};
	Token beginToken = (Token) {TOKEN_BEGIN_REFLECT, invalidAtom};
	Token endToken = (Token) {TOKEN_END_REFLECT, invalidAtom};

	PartBuilder builder;
	InitializePartBuilder(&builder, FORMULA_TOP_SCOPE);

	// reset after the first opening bracket
	ASSERT_TRUE(PartBuilderPush(&builder, nameToken))
	ASSERT_TRUE(PartBuilderPush(&builder, beginToken))
	PartBuilderReset(&builder);
	ASSERT_TRUE(PartBuilderIsEmpty(&builder))

	// reset after the first closing bracket of a relation
	ASSERT_TRUE(PartBuilderPush(&builder, nameToken))
	ASSERT_TRUE(PartBuilderPush(&builder, beginToken))
	ASSERT_TRUE(PartBuilderPush(&builder, beginToken))
	ASSERT_TRUE(PartBuilderPush(&builder, innerNameToken))
	ASSERT_TRUE(PartBuilderPush(&builder, numberToken))
	ASSERT_TRUE(PartBuilderPush(&builder, endToken))
	ASSERT_FALSE(PartBuilderComplete(&builder))
	ASSERT_FALSE(PartBuilderPush(&builder, innerNameToken))
	PartBuilderReset(&builder);
	ASSERT_TRUE(PartBuilderIsEmpty(&builder))

	// a complete relation
	ASSERT_TRUE(PartBuilderPush(&builder, nameToken))
	ASSERT_TRUE(PartBuilderPush(&builder, beginToken))
	ASSERT_TRUE(PartBuilderPush(&builder, beginToken))
	ASSERT_TRUE(PartBuilderPush(&builder, innerNameToken))
	ASSERT_TRUE(PartBuilderPush(&builder, numberToken))
	ASSERT_TRUE(PartBuilderPush(&builder, endToken))
	ASSERT_TRUE(PartBuilderPush(&builder, endToken))
	ASSERT_TRUE(PartBuilderComplete(&builder))
	ASSERT_UINT32_EQUAL(PartBuilderGetActor(&builder).type, AT_RELATION)
	ASSERT_FALSE(PartBuilderPush(&builder, endToken))
	PartBuilderReset(&builder);
	ASSERT_TRUE(PartBuilderIsEmpty(&builder))

	ReleaseTypedAtom(nameToken.typedAtom);
	ReleaseTypedAtom(innerNameToken.typedAtom);
}


/* CLAUDE: A reflected name [name] is an AT_NAME actor, also within a reflection. */
static void testReflectedName(void)
{
	Atom bar = CreateNameFromCString("bar");

	Atom expected = createTermWithReflection("foo", AT_NAME, bar, "baz", 1);
	Atom parsed = CStringToTerm("foo [bar] baz 1");
	ASSERT_TRUE(SameAtoms(parsed, expected))
	ReleaseFormula(parsed);
	parsed = CStringToTerm("foo [ bar ] baz 1");
	ASSERT_TRUE(SameAtoms(parsed, expected))
	ReleaseFormula(parsed);
	ReleaseFormula(expected);

	// a reflected name inside a formula reflection and inside a relation
	Atom inner = createTermWithReflection("foo", AT_NAME, bar, "baz", 2);
	expected = createTermWithReflection("term", AT_FORMULA, inner, "arity", 3);
	parsed = CStringToTerm("term [foo [bar] baz 2] arity 3");
	ASSERT_TRUE(SameAtoms(parsed, expected))
	ReleaseFormula(parsed);
	// the first ] closes the reflected name, the second ] the formula reflection
	parsed = CStringToTerm("term [baz 2 foo [bar]] arity 3");
	ASSERT_TRUE(SameAtoms(parsed, expected))
	ReleaseFormula(parsed);
	ReleaseFormula(expected);

	expected = createTermWithReflection("term", AT_RELATION, inner, "arity", 3);
	parsed = CStringToTerm("term [[foo [bar] baz 2]] arity 3");
	ASSERT_TRUE(SameAtoms(parsed, expected))
	ReleaseFormula(parsed);
	ReleaseFormula(expected);

	ReleaseFormula(inner);
	NameRelease(bar);
}


/* CLAUDE: Invalid reflected name syntax, reported by ParseFormula() at the offending token. */
static void testReflectedNameRejected(void)
{
	index32 errorPosition;

	// a name cannot be reflected inside a relation
	ASSERT_UINT64_EQUAL(ParseFormula("foo [[bar]]", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 9)

	// a ] after a role name closes nothing but a reflected name
	ASSERT_UINT64_EQUAL(ParseFormula("foo [bar 1 baz]", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 14)
	ASSERT_UINT64_EQUAL(ParseFormula("foo 1 bar]", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 9)

	// an unterminated reflected name is reported at the end of the string
	ASSERT_UINT64_EQUAL(ParseFormula("foo [bar", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 8)

	// a reflected name is an actor, so a role name follows it
	ASSERT_UINT64_EQUAL(ParseFormula("foo [bar] [baz]", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 10)
}


/* CLAUDE: A part builder holding a name decides at the next token between a reflected name
   and a formula reflection. Resetting the part builder while the name is held releases it. */
static void testReflectedNamePartBuilder(void)
{
	Token nameToken = (Token) {
		TOKEN_NAME,
		CreateTypedAtom(AT_NAME, CreateNameFromCString("foo"))
	};
	Token innerNameToken = (Token) {
		TOKEN_NAME,
		CreateTypedAtom(AT_NAME, CreateNameFromCString("bar"))
	};
	Token numberToken = (Token) {TOKEN_NUMBER, CreateTypedAtom(AT_INT, (Atom) {._int = 1})};
	Token beginToken = (Token) {TOKEN_BEGIN_REFLECT, invalidAtom};
	Token endToken = (Token) {TOKEN_END_REFLECT, invalidAtom};

	PartBuilder builder;
	InitializePartBuilder(&builder, FORMULA_TOP_SCOPE);

	// a ] is never an actor
	ASSERT_TRUE(PartBuilderPush(&builder, nameToken))
	ASSERT_FALSE(PartBuilderPush(&builder, endToken))
	PartBuilderReset(&builder);

	// reset while the name is held
	ASSERT_TRUE(PartBuilderPush(&builder, nameToken))
	ASSERT_TRUE(PartBuilderPush(&builder, beginToken))
	ASSERT_TRUE(PartBuilderPush(&builder, innerNameToken))
	PartBuilderReset(&builder);
	ASSERT_TRUE(PartBuilderIsEmpty(&builder))

	// a reflected name
	ASSERT_TRUE(PartBuilderPush(&builder, nameToken))
	ASSERT_TRUE(PartBuilderPush(&builder, beginToken))
	ASSERT_TRUE(PartBuilderPush(&builder, innerNameToken))
	ASSERT_TRUE(PartBuilderPush(&builder, endToken))
	ASSERT_TRUE(PartBuilderComplete(&builder))
	ASSERT_TRUE(SameTypedAtoms(PartBuilderGetActor(&builder), innerNameToken.typedAtom))
	PartBuilderReset(&builder);

	// a formula reflection beginning with the same name
	ASSERT_TRUE(PartBuilderPush(&builder, nameToken))
	ASSERT_TRUE(PartBuilderPush(&builder, beginToken))
	ASSERT_TRUE(PartBuilderPush(&builder, innerNameToken))
	ASSERT_TRUE(PartBuilderPush(&builder, numberToken))
	ASSERT_TRUE(PartBuilderPush(&builder, endToken))
	ASSERT_TRUE(PartBuilderComplete(&builder))
	ASSERT_UINT32_EQUAL(PartBuilderGetActor(&builder).type, AT_FORMULA)
	PartBuilderReset(&builder);
	ASSERT_TRUE(PartBuilderIsEmpty(&builder))

	ReleaseTypedAtom(nameToken.typedAtom);
	ReleaseTypedAtom(innerNameToken.typedAtom);
}


int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testPartBuilder);
	ExecuteTest(testPredicateBuilder);
	ExecuteTest(testTermBuilder);
	ExecuteTest(testClauseBuilder);
	ExecuteTest(testCStringToPredicate);
	ExecuteTest(testCStringToSignature);
	ExecuteTest(testCStringToClause);
	ExecuteTest(testCStringToConjunction);
	ExecuteTest(testCStringToFormula);
	ExecuteTest(testLetterActor);
	ExecuteTest(testGeneratorActor);
	ExecuteTest(testParseFormula);
	ExecuteTest(testParseTermForm);
	ExecuteTest(testParseActors);
	ExecuteTest(testRepeatedTermRejected);
	ExecuteTest(testReflectedTerm);
	ExecuteTest(testReflectedNegatedTerm);
	ExecuteTest(testReflectedTermWithQuote);
	ExecuteTest(testReflectedClause);
	ExecuteTest(testReflectedConjunction);
	ExecuteTest(testNestedReflection);
	ExecuteTest(testReflectionRejected);
	ExecuteTest(testReflectedRelation);
	ExecuteTest(testNestedRelation);
	ExecuteTest(testRelationRejected);
	ExecuteTest(testRelationPartBuilderReset);
	ExecuteTest(testReflectedName);
	ExecuteTest(testReflectedNameRejected);
	ExecuteTest(testReflectedNamePartBuilder);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}
