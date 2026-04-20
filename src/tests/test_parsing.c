
#include "kernel/FloatIEEE754.h"
#include "lang/Variable.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "lang/ClauseForm.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "parser/ClauseBuilder.h"
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
		.atom = CreateVariable('x')
	};	
	fixture->actorTokens[1] = (Token) {
		.type = TOKEN_NUMBER,
		.atom = CreateFloat32(123.45)
	};
	fixture->actorTokens[2] = (Token) {
		.type = TOKEN_STRING,
		.atom = CreateTypedAtom(AT_ID, CreateStringFromCString("foobar"))
	};

	for(index8 i = 0; i < EXAMPLE_N_PARTS; i++) {
		fixture->names[i] = fixture->nameTokens[i].atom.atom;
		fixture->actors[i] = fixture->actorTokens[i].atom;
	}

}


static void teardownTokensFixture(TokensFixture * fixture)
{
	for(index8 i = 0; i < EXAMPLE_N_PARTS; i++) {
		ReleaseTypedAtom(fixture->nameTokens[i].atom);
		ReleaseTypedAtom(fixture->actorTokens[i].atom);
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

		ASSERT_DATA64_EQUAL(PartBuilderGetRole(&builder), fixture.names[i])

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
	IFactRelease(fixture->predicate);
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

	ASSERT_DATA64_EQUAL(predicate, fixture.predicate)
	// the forms are identical
	ASSERT_DATA64_EQUAL(
		FormulaGetForm(predicate), FormulaGetForm(fixture.predicate)
	)
	// the actor lists are identical
	ASSERT_DATA64_EQUAL(FormulaGetActors(predicate), FormulaGetActors(fixture.predicate))

	IFactRelease(predicate);
	CleanupPredicateBuilder(&builder);

	teardownPredicateFixture(&fixture);
}


typedef struct {
	PredicateFixture predicateFixture;
	Atom term, negatedTerm;
} TermFixture;


static void setupTermFixture(TermFixture * fixture)
{
	setupPredicateFixture(&(fixture->predicateFixture));
	fixture->term = CreateTerm(fixture->predicateFixture.predicate, true);
	fixture->negatedTerm = CreateTerm(fixture->predicateFixture.predicate, false);
}

static void teardownTermFixture(TermFixture * fixture)
{
	IFactRelease(fixture->term);
	IFactRelease(fixture->negatedTerm);
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
		ASSERT_DATA64_EQUAL(term, fixtureTerm)

		IFactRelease(term);
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
	IFactRelease(fixture->clause);
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
	}
	Atom clause = ClauseBuilderCreateFormula(&builder);
	CleanupClauseBuilder(&builder);

	ASSERT_DATA64_EQUAL(clause, fixture.clause)

	Atom actorsList = FormulaGetActors(clause);
	ASSERT_UINT32_EQUAL(ListLength(actorsList), EXAMPLE_CLAUSE_ARITY)

	IFactRelease(clause);

	teardownClauseFixture(&fixture);
}


static void testCStringToPredicate(void)
{
	char const * exampleString = "foo 123 baz \"foobar\" bar 456 bar 789";
	Atom predicate = CStringToPredicate(exampleString);
	// PrintFormula(predicate);
	// PrintChar('\n');

	Atom predicateForm = FormulaGetForm(predicate);
	ASSERT_UINT32_EQUAL(PredicateArity(predicateForm), 4)

	Atom actorsList = FormulaGetActors(predicate);
	ASSERT_UINT32_EQUAL(ListLength(actorsList), 4)

	Atom string = CreateStringFromCString("foobar");
	ASSERT_TRUE(
		SameTypedAtoms(
			ListGetElement(actorsList, 2),
			CreateTypedAtom(AT_ID, string)
		)
	)
	IFactRelease(string);

	IFactRelease(predicate);
}


static void testCStringToClause(void)
{
	
	// NOTE: this string must be in canonical order
	char const * exampleString = "foo _x bar 123.45 | aarf \"foobar\"";
	Atom clause = CStringToClause(exampleString, CStringLength(exampleString));
	// PrintFormula(clause);
	// PrintChar('\n');

	Atom clauseForm = FormulaGetForm(clause);
	ASSERT_UINT32_EQUAL(ClauseArity(clauseForm), 3)

	Atom actorsList = FormulaGetActors(clause);
	ASSERT_UINT32_EQUAL(ListLength(actorsList), 3)

	ASSERT_TRUE(
		SameTypedAtoms(
			ListGetElement(actorsList, 1),
			CreateVariable('x')
		)
	)
	ASSERT_TRUE(
		SameTypedAtoms(
			ListGetElement(actorsList, 2),
			CreateFloat64(123.45)
		)
	)
	Atom string = CreateStringFromCString("foobar");
	ASSERT_TRUE(
		SameTypedAtoms(
			ListGetElement(actorsList, 3),
			CreateTypedAtom(AT_ID, string)
		)
	)
	IFactRelease(string);
	IFactRelease(clause);

	// TODO: more complex test cases, and conjunctions, e.g.
	// foo 42 bar 3.4 | !string "baaz" & + 2 + 2 = 4 & foobar _x | foobar _y & + 3 + 4 = 8
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

	KernelShutdown();

	TestSummary();
}
