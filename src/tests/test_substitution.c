#include "kernel/kernel.h"
#include "kernel/typedtuple.h"
#include "lang/Variable.h"
#include "lang/SubstitutionList.h"
#include "library/library.h"
#include "parser/TermBuilder.h"
#include "parser/Tokenizer.h"
#include "testing/testing.h"


void testSubstitution(void)
{
	TypedAtom x = CreateTypedAtom(AT_VARIABLE, CreateVariable('x'));
	TypedAtom y = CreateTypedAtom(AT_VARIABLE, CreateVariable('y'));
	TypedAtom one = CreateTypedAtom(AT_INT, (Atom) {._int = 1});
	TypedAtom two = CreateTypedAtom(AT_INT, (Atom) {._int = 2});

	// Substitution {x -> y}
	Substitution subst;
	SetupSubstitution(&subst, 10);
	SubstitutionAdd(&subst, x, y);

	TypedTuple * tuple = CreateTypedTupleFromArray(
		(TypedAtom[]) {	one, x, two, y }, 4
	);
	TypedTuple * result = CreateTypedTuple(4);
	SubstituteTuple(&subst, tuple, result);

	// The result should be (1 y 2 y)
	TypedTuple * expectedResult = CreateTypedTupleFromArray(
		(TypedAtom[]) {	one, y, two, y }, 4
	);
	ASSERT_TRUE(TypedTupleEqual(result, expectedResult));

	FreeSubstitution(&subst);
	FreeTypedTuple(tuple);
	FreeTypedTuple(expectedResult);
	FreeTypedTuple(result);
}

/**
 * Test substituting a variable in a reflection
 */
void testSubstituteReflection(void)
{
	TypedAtom x = CreateTypedAtom(AT_VARIABLE, CreateVariable('x'));
	TypedAtom y = CreateTypedAtom(AT_VARIABLE, CreateVariable('y'));
	TypedAtom z = CreateTypedAtom(AT_VARIABLE, CreateVariable('z'));
	TypedAtom one = CreateTypedAtom(AT_INT, (Atom) {._int = 1});
	TypedAtom two = CreateTypedAtom(AT_INT, (Atom) {._int = 2});
	// Create the (reflected) formula [foo ^x bar x baz y barf ^z]
	// Note that a quoted variable ^x is distinct from an unquoted variable x.
	// Here we need to parse in reflecton scope
	TermBuilder builder;
	InitializeTermBuilder(&builder, FORMULA_REFLECTED_SCOPE);
	TokenizeCString("foo ^x bar x baz y barf ^z", TermBuilderTokenHandler, &builder);
	ASSERT_TRUE(TermBuilderIsValid(&builder))
	TypedAtom formula = CreateTypedAtom(AT_FORMULA, TermBuilderCreateFormula(&builder, 0));

	// Substitution {x -> y, z -> 2}
	Substitution subst;
	SetupSubstitution(&subst, 10);
	SubstitutionAdd(&subst, x, y);
	SubstitutionAdd(&subst, z, two);

	// The tuple (1 x 2 [foo ^x bar x baz y barf ^z])
	TypedTuple * tuple = CreateTypedTupleFromArray(
		(TypedAtom[]) {	one, x, two, formula }, 4
	);
	TypedTuple * result = CreateTypedTuple(4);
	SubstituteTuple(&subst, tuple, result);

	// The result should be (1 y 2 [foo ^y barx  baz y barf 2])
	TermBuilderReset(&builder);
	TokenizeCString("foo ^y bar x baz y barf 2", TermBuilderTokenHandler, &builder);
	ASSERT_TRUE(TermBuilderIsValid(&builder))
	TypedAtom substitutedFormula = CreateTypedAtom(AT_FORMULA, TermBuilderCreateFormula(&builder, 0));
	TermBuilderFree(&builder);

	TypedTuple * expectedResult = CreateTypedTupleFromArray(
		(TypedAtom[]) {	one, y, two, substitutedFormula }, 4
	);
	ASSERT_TRUE(TypedTupleEqual(result, expectedResult));

	FreeTypedTuple(expectedResult);
	FreeTypedTuple(result);
	ReleaseFormula(substitutedFormula.atom);
	ReleaseFormula(formula.atom);
	FreeSubstitution(&subst);
	FreeTypedTuple(tuple);
}



int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testSubstitution);
	ExecuteTest(testSubstituteReflection);

	UnloadLibraries();
	KernelShutdown();
	TestSummary();
}
