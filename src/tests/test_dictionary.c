
#include "kernel/dictionary.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/string.h"
#include "lang/ClauseForm.h"
#include "parser/ClauseBuilder.h"
#include "testing/testing.h"


void testDictionary(void)
{
	Atom rule = CStringToClause("!number _x square _s | * _x * _x = _s");

	DictionaryAddClause(rule);

	DictionaryRemoveClause(rule);

	IFactRelease(rule);
}


void testDictionaryIterator(void)
{
	// create a form

	// iterate over matching rules
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testDictionary);

	KernelShutdown();

	TestSummary();
}

