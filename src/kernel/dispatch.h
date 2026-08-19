
/**
 * The dispatcher accepts a query and finds a matching service within the current process.
 *
 * A query is matched by its *type*: the term form, together with the direction and atom
 * type of each parameter. DispatchQuery() takes the actors of a query and generalizes
 * them to parameters itself; the other entry points take a query already generalized, as
 * the compiler works with parameters throughout. See GetQueryParameters().
 */

#ifndef DISPATCH_H
#define DISPATCH_H

#include "kernel/Parameter.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"

/**
 * Dispatch a query, copying the first matching service to *service, if any.
 * Returns true if a match was found.
 * The argument permutation required to match the service is written
 * to the given permutation array, such that queryActors element permutation[i]
 * matches service parameter i.
 *
 * NOTE: we return Services here rather than just the associated Operators,
 * since we often want to know the atom (column) types of the matched service.
 *
 * The query holds the actors of a query and no parameter of its own, which DEBUG builds
 * assert: a caller working in parameters wants DispatchGeneralizedQuery().
 *
 * NOTE: the query is generalized to its type, so an actor occurring at several positions
 * is matched as if each occurrence were an actor of its own: services do not (currently)
 * allow repeated parameters. A matching service therefore yields tuples the query did not
 * ask for when it repeats an actor, and the caller has to filter them; see
 * MixedTypeRelation.h.
 */
bool DispatchQuery(
	Atom queryTermForm, TypedTuple const * queryActors, Service * service, index8 permutation[]);

/**
 * Same, using a term (formula) instead of a termform and actors tuple
 */
bool DispatchQueryFormula(Formula * queryTerm, Service * service, index8 * permutation);

/**
 * Dispatch a query already generalized to parameters, which the tuple must hold: matching
 * is signature against signature. Unlike DispatchQuery(), a parameter occurring at
 * several positions is matched as one atom, which is what a rule body term with a
 * repeated variable needs.
 *
 * Skips the nSkip first matching services instead of returning the first service, and
 * sets *hasNextMatch = true if at least one additional match exists beyond the one
 * returned. Caller can pass hasNextMatch = 0 if only one match is required.
 *
 * Several services may match when the query leaves an output parameter
 * untyped, since the type is then unconstrained: every relation table
 * registered for the form is a candidate. The compiler uses this to compile
 * one service per candidate; see compiler.c.
 */
bool DispatchGeneralizedQuery(
	Atom queryTermForm, TypedTuple const * queryParameters, Service * service,
	index8 permutation[], size8 nSkip, bool * hasNextMatch);


/**
 * Iterating over the services matching a query. A caller that wants every matching
 * service should use the iterator rather than calling DispatchGeneralizedQuery() once per
 * match, which repeats the search from the start every time.
 */
typedef struct {
	TypedTuple const * queryParameters;
	index8 * permutation;
	RelationIterator relationIterator;
	ServiceIterator serviceIterator;
	// whether serviceIterator is positioned within the services of a relation table
	bool inRelation;
	Service service;
#ifdef DEBUG
	// Relation of the previous match, kept to verify that one query never matches two
	// services of one relation; see ServiceRegistryAdd()
	RelationTable const * previousMatchRelation;
#endif
} DispatchIterator;

/**
 * Create an iterator over the services matching the given query, which must be
 * generalized to parameters as for DispatchGeneralizedQuery(), and which must remain
 * valid until the iterator is ended. The services are visited in the same order as
 * DispatchGeneralizedQuery() with increasing nSkip.
 * The iterator is positioned before the first matching service, so
 * DispatchIteratorNext() must be called before DispatchIteratorPeekService().
 * The permutation array must hold at least queryParameters->nAtoms elements,
 * and receives the argument permutation of the current match; see DispatchQuery().
 * The caller must call DispatchIteratorEnd() when done.
 */
void DispatchIterate(
	Atom queryTermForm, TypedTuple const * queryParameters, index8 permutation[],
	DispatchIterator * iterator);

/**
 * Advance to the next matching service, if one exists, writing its argument
 * permutation to the permutation array given to DispatchIterate().
 */
bool DispatchIteratorNext(DispatchIterator * iterator);

/**
 * The service at the current iterator position.
 * Only valid after DispatchIteratorNext() has returned true, and until the next
 * call to DispatchIteratorNext() or DispatchIteratorEnd().
 */
Service const * DispatchIteratorPeekService(DispatchIterator const * iterator);

void DispatchIteratorEnd(DispatchIterator * iterator);


#endif	// DISPATCH_H
