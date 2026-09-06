
#include "kernel/kernel.h"
#include "lang/Variable.h"
#include "testing/testing.h"


void testVariable(void)
{
	Atom var1 = CreateVariable('X');
	ASSERT_CHAR_EQUAL(GetVariableName(var1), 'x')

	// variables are always lowercase
	Atom var2 = CreateVariable('y');
	ASSERT_CHAR_EQUAL(GetVariableName(var2), 'y')

	Atom var3 = anonymousVariable.atom;
	ASSERT_CHAR_EQUAL(GetVariableName(var3), '_')

	// test quoting
	ASSERT_FALSE(VariableIsQuoted(var1))
	Atom quotedVar1 = QuoteVariable(var1);
	ASSERT_TRUE(VariableIsQuoted(quotedVar1))
	ASSERT_DATA64_EQUAL(UnquoteVariable(quotedVar1).hash, var1.hash)
}


/**
 * A named variable is identified by its name, so two occurrences of the same
 * name are the same variable.
 */
static void testSameNamedVariable(void)
{
	Atom x = CreateVariable('x');
	Atom y = CreateVariable('y');

	ASSERT_TRUE(SameVariable(x, x))
	ASSERT_FALSE(SameVariable(x, y))

	// a name is case-insensitive, so these are two occurrences of one variable
	ASSERT_TRUE(SameVariable(CreateVariable('x'), CreateVariable('X')))
}


/**
 * The anonymous variable _ compares unequal to every variable including itself,
 * which is what lets one _ stand for a distinct variable at each occurrence.
 */
static void testSameAnonymousVariable(void)
{
	Atom anonymous = anonymousVariable.atom;
	Atom x = CreateVariable('x');

	ASSERT_FALSE(SameVariable(anonymous, anonymous))
	ASSERT_FALSE(SameVariable(anonymous, x))
	ASSERT_FALSE(SameVariable(x, anonymous))

	// Two occurrences of _ are two variables, although they are the same atom
	ASSERT_TRUE(SameAtoms(anonymous, anonymous))

	// A variable is anonymous by having no name, whatever else it carries.
	// Quoting _ is meaningless and the syntax does not produce it, but building
	// the atom must not make _ compare equal to anything; see QuoteVariable().
	ASSERT_FALSE(SameVariable(QuoteVariable(anonymous), QuoteVariable(anonymous)))
	ASSERT_FALSE(SameVariable(QuoteVariable(anonymous), anonymous))
}


/**
 * A quote is part of the identity of a variable; see QuoteVariable().
 */
static void testSameQuotedVariable(void)
{
	Atom x = CreateVariable('x');
	Atom quotedX = QuoteVariable(x);

	ASSERT_FALSE(SameVariable(quotedX, x))
	ASSERT_TRUE(SameVariable(quotedX, QuoteVariable(x)))
	ASSERT_TRUE(SameVariable(UnquoteVariable(quotedX), x))
}


int main(int argc, char * argv[])
{
	SetupMemory();

	ExecuteTest(testVariable);
	ExecuteTest(testSameNamedVariable);
	ExecuteTest(testSameAnonymousVariable);
	ExecuteTest(testSameQuotedVariable);

	CleanupMemory();

	TestSummary();
}
