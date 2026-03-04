
#include "datumtypes/Variable.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "lang/Quote.h"

#include "testing/testing.h"


void testQuote(void)
{
	Atom variable = CreateVariable('x');
	
	Atom quoted = CreateQuote(variable);
	ASSERT_TRUE(IsQuote(quoted))
	ASSERT_TRUE(SameAtoms(QuoteGetQuoted(quoted), variable));

	IFactRelease(quoted);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testQuote);

	KernelShutdown();

	TestSummary();
}
