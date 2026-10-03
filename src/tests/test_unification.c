#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/typedtuple.h"
#include "lang/Variable.h"
#include "lang/unification.h"
#include "library/library.h"
#include "testing/testing.h"


void testUnification(void)
{
	// Unify tuples (1 x 2 y) and (1 3 z z)
	TypedAtom x = CreateTypedAtom(AT_VARIABLE, CreateVariable('x'));
	TypedAtom y = CreateTypedAtom(AT_VARIABLE, CreateVariable('y'));
	TypedAtom one = CreateTypedAtom(AT_INT, (Atom) {._int = 1});
	TypedAtom two = CreateTypedAtom(AT_INT, (Atom) {._int = 2});
	TypedTuple * tuple1 = CreateTypedTupleFromArray(
		(TypedAtom[]) {	one, x, two, y },
		4
	);
	TypedAtom z = CreateTypedAtom(AT_VARIABLE, CreateVariable('z'));
	TypedAtom three = CreateTypedAtom(AT_INT, (Atom) {._int = 3});
	TypedTuple * tuple2 = CreateTypedTupleFromArray(
		(TypedAtom[]) { one, three, z, z },
		4
	);
	Substitution subst;
	UnifyTuples(tuple1, tuple2, &subst);

	// This should produce the unifying substitution
	// {x -> 3 y -> 2, z -> 2 }
	ASSERT_TRUE(SameTypedAtoms(SubstitutionFindValue(&subst, x), three));
	ASSERT_TRUE(SameTypedAtoms(SubstitutionFindValue(&subst, y), two));
	ASSERT_TRUE(SameTypedAtoms(SubstitutionFindValue(&subst, z), two));

	FreeSubstitution(&subst);
	FreeTypedTuple(tuple1);
	FreeTypedTuple(tuple2);
}


/*
 * Unify tuple1 with tuple2 and verify that the substitution list has the expected length,
 * and unifies boths tuples into the expected tuple.
 */
static void assertUnifiesTo(
	TypedTuple const * tuple1, TypedTuple const * tuple2, TypedTuple const * expectedTuple,
	size8 expectedNPairs)
{
	Substitution subst;
	ASSERT_TRUE(UnifyTuples(tuple1, tuple2, &subst))
	ASSERT_UINT32_EQUAL(subst.nPairs, expectedNPairs)

	TypedTuple * unified1 = CreateTypedTuple(tuple1->nAtoms);
	TypedTuple * unified2 = CreateTypedTuple(tuple2->nAtoms);
	SubstituteTuple(&subst, tuple1, unified1);
	SubstituteTuple(&subst, tuple2, unified2);
	ASSERT_TRUE(TypedTupleEqual(unified1, expectedTuple))
	ASSERT_TRUE(TypedTupleEqual(unified2, expectedTuple))

	FreeTypedTuple(unified1);
	FreeTypedTuple(unified2);
	FreeSubstitution(&subst);
}


/*
 * CLAUDE: A variable bound to a variable that is later bound to a constant
 * must be substituted by the constant.
 */
void testUnificationChain(void)
{
	TypedAtom x = CreateTypedAtom(AT_VARIABLE, CreateVariable('x'));
	TypedAtom y = CreateTypedAtom(AT_VARIABLE, CreateVariable('y'));
	TypedAtom z = CreateTypedAtom(AT_VARIABLE, CreateVariable('z'));
	TypedAtom three = CreateTypedAtom(AT_INT, (Atom) {._int = 3});

	// (x x) and (y 3) unify to (3 3)
	TypedTuple * xx = CreateTypedTupleFromArray((TypedAtom[]) {x, x}, 2);
	TypedTuple * y3 = CreateTypedTupleFromArray((TypedAtom[]) {y, three}, 2);
	TypedTuple * threeThree = CreateTypedTupleFromArray((TypedAtom[]) {three, three}, 2);
	assertUnifiesTo(xx, y3, threeThree, 2);
	assertUnifiesTo(y3, xx, threeThree, 2);

	// (x 3) and (y y) unify to (3 3)
	TypedTuple * x3 = CreateTypedTupleFromArray((TypedAtom[]) {x, three}, 2);
	TypedTuple * yy = CreateTypedTupleFromArray((TypedAtom[]) {y, y}, 2);
	assertUnifiesTo(x3, yy, threeThree, 2);

	// (x x z) and (y z 3) unify to (3 3 3)
	TypedTuple * xxz = CreateTypedTupleFromArray((TypedAtom[]) {x, x, z}, 3);
	TypedTuple * yz3 = CreateTypedTupleFromArray((TypedAtom[]) {y, z, three}, 3);
	TypedTuple * threeThreeThree = CreateTypedTupleFromArray((TypedAtom[]) {three, three, three}, 3);
	assertUnifiesTo(xxz, yz3, threeThreeThree, 3);

	FreeTypedTuple(xx);
	FreeTypedTuple(y3);
	FreeTypedTuple(threeThree);
	FreeTypedTuple(x3);
	FreeTypedTuple(yy);
	FreeTypedTuple(xxz);
	FreeTypedTuple(yz3);
	FreeTypedTuple(threeThreeThree);
}


static TypedAtom createParameter(uint8 number, byte io, byte atomType)
{
	return CreateTypedAtom(
		AT_PARAMETER,
		(Atom) {.parameter = {.number = number, .io = io, .atomType = atomType}});
}


/*
 * A parameter binds to a constant of its own type, and so does any variable
 * bound to the parameter. A parameter never binds to another parameter.
 */
void testUnificationParameters(void)
{
	TypedAtom x = CreateTypedAtom(AT_VARIABLE, CreateVariable('x'));
	TypedAtom y = CreateTypedAtom(AT_VARIABLE, CreateVariable('y'));
	TypedAtom seven = CreateTypedAtom(AT_INT, (Atom) {._int = 7});
	TypedAtom p1 = createParameter(1, PARAMETER_IN, AT_INT);
	TypedAtom p2 = createParameter(2, PARAMETER_OUT, 0);

	// (#1<INT #1<INT #2>) and (7 x y) unify to (7 7 #2>)
	TypedTuple * p1p1p2 = CreateTypedTupleFromArray((TypedAtom[]) {p1, p1, p2}, 3);
	TypedTuple * sevenXY = CreateTypedTupleFromArray((TypedAtom[]) {seven, x, y}, 3);
	TypedTuple * sevenSevenP2 = CreateTypedTupleFromArray((TypedAtom[]) {seven, seven, p2}, 3);
	assertUnifiesTo(p1p1p2, sevenXY, sevenSevenP2, 3);

	// (#1<INT #1<INT) and (x 7) unify to (7 7)
	TypedTuple * p1p1 = CreateTypedTupleFromArray((TypedAtom[]) {p1, p1}, 2);
	TypedTuple * x7 = CreateTypedTupleFromArray((TypedAtom[]) {x, seven}, 2);
	TypedTuple * sevenSeven = CreateTypedTupleFromArray((TypedAtom[]) {seven, seven}, 2);
	assertUnifiesTo(p1p1, x7, sevenSeven, 2);

	Substitution subst;

	// An INT parameter does not unify with a FLOAT constant
	TypedAtom sevenFloat = CreateTypedAtom(AT_FLOAT, (Atom) {._float = 7.0});
	TypedTuple * p1Only = CreateTypedTupleFromArray((TypedAtom[]) {p1}, 1);
	TypedTuple * floatOnly = CreateTypedTupleFromArray((TypedAtom[]) {sevenFloat}, 1);
	ASSERT_FALSE(UnifyTuples(p1Only, floatOnly, &subst))
	FreeSubstitution(&subst);

	// (#1<INT #2>) and (x x) does not unify
	TypedTuple * p1p2 = CreateTypedTupleFromArray((TypedAtom[]) {p1, p2}, 2);
	TypedTuple * xx = CreateTypedTupleFromArray((TypedAtom[]) {x, x}, 2);
	ASSERT_FALSE(UnifyTuples(p1p2, xx, &subst))
	FreeSubstitution(&subst);

	FreeTypedTuple(p1p1p2);
	FreeTypedTuple(sevenXY);
	FreeTypedTuple(sevenSevenP2);
	FreeTypedTuple(p1p1);
	FreeTypedTuple(x7);
	FreeTypedTuple(sevenSeven);
	FreeTypedTuple(p1Only);
	FreeTypedTuple(floatOnly);
	FreeTypedTuple(p1p2);
	FreeTypedTuple(xx);
}


int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testUnification);
	ExecuteTest(testUnificationChain);
	ExecuteTest(testUnificationParameters);

	UnloadLibraries();
	KernelShutdown();
	TestSummary();
}
