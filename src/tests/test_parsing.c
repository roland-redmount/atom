
#include "kernel/float.h"
#include "lang/Variable.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/string.h"
#include "lang/ClauseForm.h"
#include "lang/ConjunctionForm.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "parser/ClauseBuilder.h"
#include "parser/ConjunctionBuilder.h"
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
	Formula * predicate;
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
	FreeFormula(fixture->predicate);
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
	Formula * predicate = PredicateBuilderCreateFormula(&builder);

	ASSERT_TRUE(FormulaEqual(predicate, fixture.predicate))

	FreeFormula(predicate);
	CleanupPredicateBuilder(&builder);

	teardownPredicateFixture(&fixture);
}


typedef struct {
	PredicateFixture predicateFixture;
	Formula * term;
	Formula * negatedTerm;
} TermFixture;


static void setupTermFixture(TermFixture * fixture)
{
	setupPredicateFixture(&(fixture->predicateFixture));
	fixture->term = CreateTerm(fixture->predicateFixture.predicate, true);
	fixture->negatedTerm = CreateTerm(fixture->predicateFixture.predicate, false);
}

static void teardownTermFixture(TermFixture * fixture)
{
	FreeFormula(fixture->term);
	FreeFormula(fixture->negatedTerm);
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
		Formula * term = TermBuilderCreateFormula(&builder);

		Formula * fixtureTerm = sign ? fixture.term : fixture.negatedTerm;
		ASSERT_TRUE(FormulaEqual(term, fixtureTerm))

		FreeFormula(term);
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
	const Formula * terms[EXAMPLE_CLAUSE_N_TERMS];
	Formula * clause;
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
	FreeFormula(fixture->clause);
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
	Formula * clause = ClauseBuilderCreateFormula(&builder);
	CleanupClauseBuilder(&builder);

	ASSERT_TRUE(FormulaEqual(clause, fixture.clause))

	FreeFormula(clause);
	teardownClauseFixture(&fixture);
}


static void testCStringToPredicate(void)
{
	char const * exampleString = "foo 123 baz \"foobar\" bar 456 bar 789";
	Formula * predicate = CStringToPredicate(exampleString);
	// PrintFormula(predicate);
	// PrintChar('\n');

	ASSERT_UINT32_EQUAL(PredicateArity(predicate->form), 4)
	ASSERT_UINT32_EQUAL(predicate->actors->nAtoms, 4)

	Atom string = CreateStringFromCString("foobar");
	ASSERT_TRUE(
		SameTypedAtoms(
			TypedTupleGetElement(predicate->actors, 1),
			CreateTypedAtom(AT_ID, string)
		)
	)
	IFactRelease(string);

	FreeFormula(predicate);
}


static void testCStringToClause(void)
{
	// NOTE: this string must be in canonical order
	Formula * clause = CStringToClause("aarf \"foobar\" | foo _x bar 123.45");
	// PrintFormula(clause);
	// PrintChar('\n');

	ASSERT_UINT32_EQUAL(ClauseArity(clause->form), 3);
	ASSERT_UINT32_EQUAL(clause->actors->nAtoms, 3)

	Atom string = CreateStringFromCString("foobar");
	ASSERT_TRUE(
		SameTypedAtoms(
			TypedTupleGetElement(clause->actors, 0),
			CreateTypedAtom(AT_ID, string)
		)
	)
	IFactRelease(string);
	ASSERT_TRUE(
		SameTypedAtoms(
			TypedTupleGetElement(clause->actors, 1),
			CreateTypedAtom(AT_VARIABLE, CreateVariable('x'))
		)
	)
	ASSERT_TRUE(
		SameTypedAtoms(
			TypedTupleGetElement(clause->actors, 2),
			CreateTypedAtom(AT_FLOAT, (Atom) {._float = 123.45})
		)
	)

	FreeFormula(clause);

	// TODO: more complex test cases, and conjunctions, e.g.
	// foo 42 bar 3.4 | !string "baaz" & + 2 + 2 = 4 & foobar _x | foobar _y & + 3 + 4 = 8
}


static void testCStringToConjunction(void)
{
	// NOTE: this string must be in canonical order
	Formula * conjunction = CStringToConjunction("aarf \"foobar\" | foo _x bar 123.45 & barf 42 frob _y");
	// PrintFormula(conjunction);
	// PrintChar('\n');

	ASSERT_UINT32_EQUAL(ConjunctionFormArity(conjunction->form), 5)
	ASSERT_UINT32_EQUAL(conjunction->actors->nAtoms, 5)

	FreeFormula(conjunction);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testPartBuilder);
	ExecuteTest(testPredicateBuilder);
	ExecuteTest(testTermBuilder);
	ExecuteTest(testClauseBuilder);
	ExecuteTest(testCStringToPredicate);
	ExecuteTest(testCStringToClause);
	ExecuteTest(testCStringToConjunction);

	KernelShutdown();

	TestSummary();
}
