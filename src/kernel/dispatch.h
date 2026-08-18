
/**
 * The dispatcher accepts a query (formula) and finds a matching service
 * within the current process.
 */

#ifndef DISPATCH_H
#define DISPATCH_H

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
 * NOTE: queryActors with repeated variables are not handled by dispatch, since
 * services do not (currently) allow repeated parameters. Therefore, the returned
 * services may return additional tuples not matching the query when repeated
 * variables are present, which must be filtered by the caller.
 * See MixedTypeRelation.h for a solution to this problem.
 */
bool DispatchQuery(Atom queryTermForm, TypedTuple const * queryActors, Service * service, index8 permutation[]);

/**
 * Similar to DispatchQuery(), but skips the nSkip first matching services instead of
 * returning the first service, and sets *hasNextMatch = true if at least one additional match exists
 * beyond the one returned. Caller can pass hasNextMatch = 0 if only one match is required.
 *
 * Several services may match when the query leaves an output parameter
 * untyped, since the type is then unconstrained: every relation table
 * registered for the form is a candidate. The compiler uses this to compile
 * one service per candidate; see compiler.c.
 */
bool DispatchQueryAt(
	Atom queryTermForm, TypedTuple const * queryActors, Service * service,
	index8 permutation[], size8 nSkip, bool * hasNextMatch);

/**
 * Same, using a term (formula) instead of a termform and actors tuple
 */
bool DispatchQueryFormula(Formula * queryTerm, Service * service, index8 * permutation);

/**
 * Set equalityMap[i] to the index of the first query actor denoting the same atom as
 * actor i, which is i itself for an actor occurring once. Only a variable or a parameter
 * denotes one atom across positions; any other actor is its own first occurence.
 * Returns true if any actor is repeated.
 *
 * NOTE: each occurence of the anonymous variable _ is a variable of its own, and so is
 * never equal to another actor.
 */
bool QueryEqualityMap(TypedTuple const * queryActors, index8 equalityMap[]);


/**
 * Iterating over the services matching a query. A caller that wants every matching
 * service should use the iterator rather than calling DispatchQueryAt() once per
 * match, which repeats the search from the start every time.
 */
typedef struct {
	TypedTuple const * queryActors;
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
 * Create an iterator over the services matching the given query, visiting the same
 * services in the same order as DispatchQueryAt() with increasing nSkip.
 * The iterator is positioned before the first matching service, so
 * DispatchIteratorNext() must be called before DispatchIteratorPeekService().
 * The permutation array must hold at least queryActors->nAtoms elements,
 * and receives the argument permutation of the current match; see DispatchQuery().
 * The caller must call DispatchIteratorEnd() when done.
 */
void DispatchQueryIterate(
	Atom queryTermForm, TypedTuple const * queryActors, index8 permutation[], DispatchIterator * iterator);

/**
 * Advance to the next matching service, if one exists, writing its argument
 * permutation to the permutation array given to DispatchQueryIterate().
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
