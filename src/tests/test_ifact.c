#include "kernel/kernel.h"
#include "library/library.h"
#include "parser/TermBuilder.h"
#include "ui/query.h"

#include "testing/testing.h"


void testCallIFactService(void)
{
	Atom query = CStringToTerm("id x ifact [named * name \"foo\"]");
	MixedTypeRelation * relation = UserQuery(query);
	size32 nTuples = 0;
	while(MixedTypeRelationNext(relation))
		nTuples++;
	ASSERT_INT32_EQUAL(nTuples, 1)

	FreeMixedTypeRelation(relation);
	ReleaseFormula(query);
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	LoadLibraries();

	ExecuteTest(testCallIFactService);

	UnloadLibraries();
	KernelShutdown();
	TestSummary();
}
