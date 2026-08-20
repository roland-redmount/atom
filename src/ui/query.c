
#include "kernel/compiler.h"
#include "kernel/dispatch.h"
#include "lang/TermForm.h"
#include "ui/query.h"


MixedTypeRelation * UserQuery(Atom queryTerm)
{
	FormulaView term = FormulaGetView(queryTerm);
	ASSERT(IsTermForm(term.form))

	// Dispatch decides whether this query has been compiled before, by its type: a
	// service answering the type answers every query of it; see DispatchQuery()
	Service service;
	index8 permutation[term.actors->nAtoms];
	if(!DispatchQuery(term.form, term.actors, &service, permutation)) {
		// Attempt to compile the query, registering new services
		Service services[MAX_COMPILED_SERVICES];
		CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	}
	return CreateConcatRelation(term.form, term.actors);
}
