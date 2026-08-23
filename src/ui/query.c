
#include "kernel/compiler.h"
#include "lang/TermForm.h"
#include "ui/query.h"


MixedTypeRelation * UserQuery(Atom queryTerm)
{
	FormulaView term = FormulaGetView(queryTerm);
	ASSERT(IsTermForm(term.form))

	// Compile the query unless a service answers it already; see FindOrCompileService()
	Service service;
	index8 permutation[term.actors->nAtoms];
	FindOrCompileService(term.form, term.actors, &service, permutation);

	return CreateConcatRelation(term.form, term.actors);
}
