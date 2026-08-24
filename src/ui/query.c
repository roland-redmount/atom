
#include "kernel/compiler.h"
#include "lang/TermForm.h"
#include "ui/query.h"


MixedTypeRelation * UserQuery(Atom queryTerm)
{
	FormulaView query = FormulaGetView(queryTerm);
	ASSERT(IsTermForm(query.form))

	// Compile the query unless a service answers it already; see FindOrCompileService()
	Service service;
	index8 permutation[query.actors->nAtoms];
	FindOrCompileService(query, &service, permutation);

	return CreateConcatRelation(query.form, query.actors);
}
