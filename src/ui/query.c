
#include "kernel/compiler.h"
#include "kernel/dispatch.h"
#include "lang/TermForm.h"
#include "ui/query.h"


MixedTypeRelation * UserQuery(Formula const * queryTerm)
{
	ASSERT(IsTermForm(queryTerm->form))

	// Dispatch decides whether this query has been compiled before, by its type: a
	// service answering the type answers every query of it; see DispatchQuery()
	Service service;
	index8 permutation[queryTerm->actors->nAtoms];
	if(!DispatchQuery(queryTerm->form, queryTerm->actors, &service, permutation)) {
		// Attempt to compile the query, registering new services
		Service services[MAX_COMPILED_SERVICES];
		CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	}
	return CreateConcatRelation(queryTerm->form, queryTerm->actors);
}
