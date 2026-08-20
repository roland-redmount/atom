
#include "kernel/float.h"
#include "lang/Variable.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/list.h"
#include "kernel/Parameter.h"
#include "kernel/string.h"
#include "lang/ClauseForm.h"
#include "lang/ConjunctionForm.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "parser/ClauseBuilder.h"
#include "parser/ConjunctionBuilder.h"
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
	InitializePartBuilder(&builder);
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
	InitializePredicateBuilder(&builder);
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
	InitializeTermBuilder(&builder);
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
	CleanupTermBuilder(&builder);

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
	InitializeClauseBuilder(&builder);
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
	Atom signature = CStringToTerm("+ @1<INT + @2<INT = @3>INT");
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

	// TODO: more complex test cases, and conjunctions, e.g.
	// foo 42 bar 3.4 | !string "baaz" & + 2 + 2 = 4 & foobar _x | foobar _y & + 3 + 4 = 8
}


static void testCStringToConjunction(void)
{
	// NOTE: this string must be in canonical order
	Atom conjunction = CStringToConjunction("aarf \"foobar\" | foo x bar 123.45 & barf 42 frob y");
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
	Atom conjunction = CStringToFormula("aarf \"foobar\" | foo x bar 123.45 & barf 42 frob y");
	ASSERT_TRUE(FormulaIsConjunction(conjunction))
	Atom expectedConjunction = CStringToConjunction("aarf \"foobar\" | foo x bar 123.45 & barf 42 frob y");
	ASSERT_TRUE(SameAtoms(conjunction, expectedConjunction))
	ReleaseFormula(expectedConjunction);
	ReleaseFormula(conjunction);
}


/**
 * Build the term (<reflectionRole> [<formula>] <numberRole> <number>),
 * with the given formula as the actor of the reflection role.
 */
static Atom createTermWithReflection(
	char const * reflectionRole, Atom formula,
	char const * numberRole, int64 number)
{
	Atom roles[2] = {
		CreateNameFromCString(reflectionRole),
		CreateNameFromCString(numberRole)
	};
	TypedAtom actors[2] = {
		CreateTypedAtom(AT_FORMULA, formula),
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
 * Parse a term holding a reflection, and compare it against the same term built
 * from a formula parsed on its own. The term is parsed both ways, since only
 * CStringToFormula() puts a clause and conjunction builder above the reflection,
 * and an operator token inside the reflection has to reach it past both of them.
 */
static void testReflection(char const * reflected, char const * termString)
{
	Atom formula = CStringToFormula(reflected);
	Atom expected = createTermWithReflection("term", formula, "arity", 2);

	Atom parsedTerm = CStringToTerm(termString);
	ASSERT_TRUE(FormulaIsTerm(parsedTerm))
	ASSERT_TRUE(SameAtoms(parsedTerm, expected))

	Atom parsedFormula = CStringToFormula(termString);
	ASSERT_TRUE(SameAtoms(parsedFormula, expected))

	ReleaseFormula(parsedFormula);
	ReleaseFormula(parsedTerm);
	ReleaseFormula(expected);
	ReleaseFormula(formula);
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
	ASSERT_UINT64_EQUAL(ParseFormula("foo x bar *", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 10)

	// a number stands where an actor does and not where a role name does, so it is
	// reported at its first character rather than at the whitespace before it
	ASSERT_UINT64_EQUAL(ParseFormula("foo x 42 bar 1", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 6)

	// a variable is named by a single letter, so a word too long to be one is reported
	// at the letter that makes it too long; see enum TokenizerMode
	ASSERT_UINT64_EQUAL(ParseFormula("foo xy bar 1", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 5)

	// a string ending in the middle of a formula is reported at its end
	ASSERT_UINT64_EQUAL(ParseFormula("foo x bar", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 9)

	// an unterminated string is read to the end of the line
	ASSERT_UINT64_EQUAL(ParseFormula("foo \"abc", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 8)

	// an unterminated reflection abandons its nested builder
	ASSERT_UINT64_EQUAL(ParseFormula("foo [ bar 1", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 11)

	// a parameter naming no known atom type is rejected, not asserted on
	ASSERT_UINT64_EQUAL(ParseFormula("foo @1<NOTATYPE", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 15)

	// a string holding no formula at all
	ASSERT_UINT64_EQUAL(ParseFormula("", &errorPosition).hash, 0)
	ASSERT_UINT32_EQUAL(errorPosition, 0)
}


static void testReflectedTerm(void)
{
	testReflection("foo \"a\" bar b", "term [foo \"a\" bar b] arity 2");
}


static void testReflectedNegatedTerm(void)
{
	testReflection("! foo 42", "term [! foo 42] arity 2");
}


static void testReflectedClause(void)
{
	testReflection("foo 1 | bar 2", "term [foo 1 | bar 2] arity 2");
}


static void testReflectedConjunction(void)
{
	testReflection("foo 1 & bar 2", "term [foo 1 & bar 2] arity 2");
}


static void testNestedReflection(void)
{
	// the expectation is built from the inside out, so that it does not
	// depend on the reflection parsing being tested
	Atom innermost = CStringToTerm("bar 1");
	Atom inner = createTermWithReflection("foo", innermost, "baz", 2);
	Atom expected = createTermWithReflection("term", inner, "arity", 3);

	Atom parsed = CStringToTerm("term [foo [bar 1] baz 2] arity 3");
	ASSERT_TRUE(SameAtoms(parsed, expected))

	ReleaseFormula(parsed);
	ReleaseFormula(expected);
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
	InitializePartBuilder(&builder);

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


int main(int argc, char * argv[])
{
	KernelInitialize();

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
	ExecuteTest(testParseFormula);
	ExecuteTest(testReflectedTerm);
	ExecuteTest(testReflectedNegatedTerm);
	ExecuteTest(testReflectedClause);
	ExecuteTest(testReflectedConjunction);
	ExecuteTest(testNestedReflection);
	ExecuteTest(testReflectionRejected);

	KernelShutdown();

	TestSummary();
}
