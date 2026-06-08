#include "kernel/kernel.h"
#include "kernel/tuple.h"
#include "lang/Variable.h"
#include "lang/unification.h"
#include "parser/ClauseBuilder.h"
#include "parser/TermBuilder.h"
#include "testing/testing.h"


void testUnification(void)
{
	// Unify tuples (1 x 2 y) and (1 3 z z)
	TypedAtom x = CreateVariable('x');
	TypedAtom y = CreateVariable('y');
	TypedAtom one = CreateTypedAtom(AT_INT, 1);
	TypedAtom two = CreateTypedAtom(AT_INT, 2);
	Tuple * tuple1 = CreateTupleFromArray(
		(TypedAtom[]) {	one, x, two, y },
		4
	);
	TypedAtom z = CreateVariable('z');
	TypedAtom three = CreateTypedAtom(AT_INT, 3);
	Tuple * tuple2 = CreateTupleFromArray(
		(TypedAtom[]) { one, three, z, z },
		4
	);
	Substitution subst1;
	Substitution subst2;
	UnifyTuples(tuple1, tuple2, &subst1, &subst2);

	// This should produce the substition lists:
	// subst1 = {x -> 3 y -> 2 }
	// subst2 = {z -> 2 }
	ASSERT_TRUE(SameTypedAtoms(SubstitutionFindValue(&subst1, x), three));
	ASSERT_TRUE(SameTypedAtoms(SubstitutionFindValue(&subst1, y), two));
	ASSERT_TRUE(SameTypedAtoms(SubstitutionFindValue(&subst2, z), two));

	FreeSubstitution(&subst1);
	FreeSubstitution(&subst2);
	FreeTuple(tuple1);
	FreeTuple(tuple2);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testUnification);

	KernelShutdown();
	TestSummary();
}
