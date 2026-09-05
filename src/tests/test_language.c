#include "kernel/Int.h"
#include "kernel/kernel.h"
#include "library/list.h"
#include "library/string.h"
#include "kernel/multiset.h"
#include "lang/ClauseForm.h"
#include "lang/formula.h"
#include "lang/FormPermutation.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"

#include "testing/testing.h"


static struct {
	Atom foo, bar, baz;
} exampleNames;


static void setup(void)
{
	// create immutable strings
	exampleNames.foo = CreateNameFromCString("foo");
	exampleNames.bar = CreateNameFromCString("bar");
	exampleNames.baz = CreateNameFromCString("baz");
}


static void teardown(void)
{
	NameRelease(exampleNames.foo);
	NameRelease(exampleNames.bar);
	NameRelease(exampleNames.baz);
}


// A canonical predicate form (bar baz^2 foo)

#define EXAMPLE_PREDICATE_ARITY 4
#define EXAMPLE_PREDICATE_N_ROLES 3

static Atom examplePredicateForm(void)
{
	return CreatePredicateForm(
		(Atom[]) {
			exampleNames.bar,
			exampleNames.baz,
			exampleNames.baz,
			exampleNames.foo
		},
		EXAMPLE_PREDICATE_ARITY
	);
}


static void testPredicateForm(void)
{
	Atom predicateForm = examplePredicateForm();
	
	ASSERT_UINT32_EQUAL(PredicateNRoles(predicateForm), EXAMPLE_PREDICATE_N_ROLES)
	ASSERT_UINT32_EQUAL(PredicateArity(predicateForm), EXAMPLE_PREDICATE_ARITY)

	// iterate over roles
	// NOTE: ordering of roles names is no longer alphanumeric, but is detemined
	// by the string (list) hash value.
	MultisetIterator roleIterator;
	MultisetIterate(predicateForm, AT_NAME, &roleIterator);
	ElementMultiple em;
	for(index32 i = 0; i < EXAMPLE_PREDICATE_N_ROLES; i++) {
		ASSERT_TRUE(MultisetIteratorNext(&roleIterator))
		em = MultisetIteratorGetElement(&roleIterator);
		if(SameAtoms(em.element, exampleNames.baz)) {
			ASSERT(em.multiple == 2)
		}
		else {
			ASSERT(em.multiple == 1)
		}
	}
	ASSERT_FALSE(MultisetIteratorNext(&roleIterator))
	MultisetIteratorEnd(&roleIterator);

	IFactRelease(predicateForm);
}


static void testTermForm(void)
{
	Atom predicateForm = examplePredicateForm();

	Atom termForm = CreateTermForm(predicateForm, true);
	ASSERT_TRUE(IsTermForm(termForm))
	ASSERT_DATA64_EQUAL(TermFormGetPredicateForm(termForm).hash, predicateForm.hash)
	ASSERT_TRUE(TermFormGetSign(termForm))
	ASSERT_UINT32_EQUAL(TermFormArity(termForm), PredicateArity(predicateForm))

	Atom termFormNegated = CreateTermForm(predicateForm, false);
	ASSERT_TRUE(IsTermForm(termFormNegated))
	ASSERT_DATA64_EQUAL(TermFormGetPredicateForm(termFormNegated).hash, predicateForm.hash)
	ASSERT_FALSE(TermFormGetSign(termFormNegated))
	ASSERT_UINT32_EQUAL(TermFormArity(termFormNegated), PredicateArity(predicateForm))

	IFactRelease(termForm);
	IFactRelease(termFormNegated);

	IFactRelease(predicateForm);
}


#define EXAMPLE_CLAUSE_N_UNIQUE_TERMS	2
#define EXAMPLE_CLAUSE_N_TERMS			3
#define EXAMPLE_CLAUSE_ARITY			(EXAMPLE_CLAUSE_N_TERMS * EXAMPLE_PREDICATE_ARITY)


typedef struct {
	Atom predicateForm;	// belongs to a predicateFormFixture
	Atom termForm;
	Atom negatedTermForm;
} TermFormsFixture;


static TermFormsFixture setupTermForms(void)
{
	TermFormsFixture fixture;
	fixture.predicateForm = examplePredicateForm();
	fixture.termForm = CreateTermForm(fixture.predicateForm, true);
	fixture.negatedTermForm = CreateTermForm(fixture.predicateForm, false);

	return fixture;
}


static void teardownTermForms(TermFormsFixture fixture)
{
	IFactRelease(fixture.termForm);
	IFactRelease(fixture.negatedTermForm);
	IFactRelease(fixture.predicateForm);
}


static Atom exampleClauseForm(TermFormsFixture termFormsFixture)
{
	Atom termForms[] = {
		termFormsFixture.negatedTermForm,
		termFormsFixture.termForm,
		termFormsFixture.termForm
	};
	return CreateClauseForm(termForms, EXAMPLE_CLAUSE_N_TERMS);
}


static void testClauseForm(void)
{
	// arrange
	TermFormsFixture termFormsFixture = setupTermForms();

	// act
	Atom clauseForm = exampleClauseForm(termFormsFixture);
	// RelationBTreeDump(RegistryGetCoreTable(FORM_PAIR_LEFT_RIGHT));

	// assert
	ASSERT_UINT32_EQUAL(ClauseFormNTermForms(clauseForm), EXAMPLE_CLAUSE_N_UNIQUE_TERMS)
	ASSERT_UINT32_EQUAL(ClauseFormNTerms(clauseForm), EXAMPLE_CLAUSE_N_TERMS)
	ASSERT_UINT32_EQUAL(ClauseArity(clauseForm), EXAMPLE_CLAUSE_ARITY)

	MultisetIterator termFormIterator;
	MultisetIterate(clauseForm, AT_ID, &termFormIterator);
	for(index8 i = 0; i < EXAMPLE_CLAUSE_N_UNIQUE_TERMS; i++) {
		ASSERT_TRUE(MultisetIteratorNext(&termFormIterator))
		ElementMultiple em = MultisetIteratorGetElement(&termFormIterator);
		// order of term forms is arbitrary
		if(SameAtoms(em.element, termFormsFixture.termForm)) {
			ASSERT_UINT32_EQUAL(em.multiple, 2)
		}
		else {
			ASSERT_DATA64_EQUAL(em.element.hash, termFormsFixture.negatedTermForm.hash)
			ASSERT_UINT32_EQUAL(em.multiple, 1)
		}
	}
	ASSERT_FALSE(MultisetIteratorNext(&termFormIterator))
	MultisetIteratorEnd(&termFormIterator);

	IFactRelease(clauseForm);

	// teardown
	teardownTermForms(termFormsFixture);
}


typedef struct {
	TermFormsFixture termFormsFixture;
	Atom clauseForm;
} ClauseFormFixture;


ClauseFormFixture setupClauseForm(void)
{
	ClauseFormFixture fixture;
	fixture.termFormsFixture = setupTermForms();
	fixture.clauseForm = exampleClauseForm(fixture.termFormsFixture);
	return fixture;
}


void teardownClauseForm(ClauseFormFixture fixture)
{
	IFactRelease(fixture.clauseForm);
	teardownTermForms(fixture.termFormsFixture);
}


static void testCreateClause(void)
{
	ClauseFormFixture clauseFormFixture = setupClauseForm();

	TypedTuple * actors = CreateTypedTuple(EXAMPLE_CLAUSE_ARITY);
	for(index8 i = 0; i < EXAMPLE_CLAUSE_ARITY; i++)
		TypedTupleSetElement(actors, i, CreateTypedAtom(AT_INT, (Atom) {._int = i + 1}));
	
	Atom clause = CreateFormula(clauseFormFixture.clauseForm, actors);

	ASSERT_UINT32_EQUAL(FormulaGetActors(clause)->nAtoms, EXAMPLE_CLAUSE_ARITY)
	ASSERT_TRUE(TypedTupleEqual(FormulaGetActors(clause), actors));

	ReleaseFormula(clause);
	FreeTypedTuple(actors);
	teardownClauseForm(clauseFormFixture);
}


/**
 * A formula is an atom, and formulas are unique: the same form and actors always
 * give the same atom, whichever way the formula is reached.
 */
static void testFormulaIsUnique(void)
{
	ClauseFormFixture clauseFormFixture = setupClauseForm();
	size32 nFormulasBefore = NumberOfFormulas();

	TypedTuple * actors = CreateTypedTuple(EXAMPLE_CLAUSE_ARITY);
	for(index8 i = 0; i < EXAMPLE_CLAUSE_ARITY; i++)
		TypedTupleSetElement(actors, i, CreateTypedAtom(AT_INT, (Atom) {._int = i + 1}));

	Atom clause = CreateFormula(clauseFormFixture.clauseForm, actors);
	ASSERT_UINT32_EQUAL(NumberOfFormulas(), nFormulasBefore + 1)

	// Creating the same formula again yields the same atom and no second formula
	Atom sameClause = CreateFormula(clauseFormFixture.clauseForm, actors);
	ASSERT_TRUE(SameAtoms(clause, sameClause))
	ASSERT_UINT32_EQUAL(NumberOfFormulas(), nFormulasBefore + 1)

	// Releasing one of the two references leaves the formula readable
	ReleaseFormula(sameClause);
	ASSERT_UINT32_EQUAL(NumberOfFormulas(), nFormulasBefore + 1)
	ASSERT_TRUE(TypedTupleEqual(FormulaGetActors(clause), actors))

	// Changing an actor gives a different formula
	TypedTupleSetElement(actors, 0, CreateTypedAtom(AT_INT, (Atom) {._int = 99}));
	Atom otherClause = CreateFormula(clauseFormFixture.clauseForm, actors);
	ASSERT_FALSE(SameAtoms(clause, otherClause))
	ASSERT_UINT32_EQUAL(NumberOfFormulas(), nFormulasBefore + 2)

	ReleaseFormula(otherClause);
	ReleaseFormula(clause);
	ASSERT_UINT32_EQUAL(NumberOfFormulas(), nFormulasBefore)

	FreeTypedTuple(actors);
	teardownClauseForm(clauseFormFixture);
}


static void testPredicatePermutation(void)
{
	// the example (foo bar baz baz) has two permutations
	// which depend on the canonical ording of names
	Atom predicateForm = examplePredicateForm();
	index8 expectedPermutations[2][EXAMPLE_PREDICATE_ARITY] = {
		{0, 1, 2, 3},
		{0, 1, 3, 2}
	};
	
	FormIterator * iterator = CreateFormIterator(predicateForm);
	index8 permutation[EXAMPLE_PREDICATE_ARITY];

	for(index32 k = 0; k < 2; k++) {
		GetTuplePermutation(iterator, permutation);
		// print permutation
		// PrintF("perm %u : ", k);
		// for(index8 i = 0; i < EXAMPLE_PREDICATE_ARITY; i++)
		// 	PrintF("%u ", permutation[i]);
		// PrintChar('\n');
	
		for(index8 i = 0; i < EXAMPLE_PREDICATE_ARITY; i++)
			ASSERT_UINT32_EQUAL(permutation[i], expectedPermutations[k][i])

		if(k < 1)
			ASSERT_TRUE(NextFormPermutation(iterator))
		else
			ASSERT_FALSE(NextFormPermutation(iterator))
	}
	FreeFormIterator(iterator);
	IFactRelease(predicateForm);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	ListSetup();
	StringSetup();

	ExecuteTestSetupTearDown(testPredicateForm, setup, teardown);
	ExecuteTestSetupTearDown(testTermForm, setup, teardown);
	ExecuteTestSetupTearDown(testClauseForm, setup, teardown);
	ExecuteTestSetupTearDown(testCreateClause, setup, teardown);
	ExecuteTestSetupTearDown(testFormulaIsUnique, setup, teardown);
	ExecuteTestSetupTearDown(testPredicatePermutation, setup, teardown);

	StringShutdown();
	ListShutdown();
	KernelShutdown();

	TestSummary();
}

