
#include "kernel/compiler.h"
#include "kernel/dispatch.h"
#include "lang/TermForm.h"
#include "ui/query.h"


/**
 * Test whether any service answers the given query type, which is the case once the
 * query has been compiled, and is also the case for a query the kernel or a stored
 * relation already answers.
 */
static bool queryTypeHasService(Atom queryTermForm, TypedTuple const * queryActors)
{
	size8 arity = queryActors->nAtoms;
	TypedTuple * parameters = CreateTypedTuple(arity);
	GetQueryParameters(queryActors, parameters);

	Service service;
	index8 permutation[arity];
	bool hasService = DispatchQuery(queryTermForm, parameters, &service, permutation);

	FreeTypedTuple(parameters);
	return hasService;
}


MixedTypeRelation * UserQuery(Formula const * queryTerm)
{
	ASSERT(IsTermForm(queryTerm->form))

	if(!queryTypeHasService(queryTerm->form, queryTerm->actors)) {
		// The compiled services are registered, and so are found by the dispatch the
		// mixed type relation performs below
		Service services[MAX_COMPILED_SERVICES];
		CompileQuery(queryTerm, services, MAX_COMPILED_SERVICES);
	}
	return CreateConcatRelation(queryTerm->form, queryTerm->actors);
}
