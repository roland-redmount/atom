
/**
 * The dispatcher accepts a query and finds a matching service within the current process.
 *
 * A query is matched as a *parameterized query*: the term form, together with the direction
 * and atom type of each parameter. DispatchQuery() takes the actors of a query and
 * parameterizes them itself; the other entry points take an array of AT_PARAMETER atoms,
 * the compiler working in parameters throughout. See GetQueryParameters().
 */

#ifndef DISPATCH_H
#define DISPATCH_H

#include "kernel/Parameter.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/formula.h"


/**
 * Dispatch a query, copying the first matching service to *service, if any.
 * The queryAtoms tuple must not contain parameters (AT_PARAMETER) atoms;
 * see DispatchParameterizedQuery().
 * Returns true if a match was found.
 * The argument permutation required to match the service is written
 * to the given permutation array, such that queryActors element permutation[i]
 * matches service parameter i.
 *
 * NOTE: we return Services here rather than just the associated Operators,
 * since we often want to know the atom (column) types of the matched service.
 *
 * NOTE: the query is parameterized so that each actor is mapped to a distinct
 * parameter, as services do not (currently) allow repeated parameters.
 * A matched service may therefore yield tuples the query did not ask for when
 * the query contains repeated variables, for example (edge e from x to x).
 * The caller must filter out these tuples; see MixedTypeRelation.h.
 */
bool DispatchQuery(FormulaView query, Service * service, index8 permutation[]);

/**
 * Same, using a term (formula) instead of a termform and actors tuple
 */
bool DispatchQueryFormula(Atom queryTerm, Service * service, index8 * permutation);


/**
 * Dispatch a parameterized query. The queryParameters array must contain AT_PARAMETER
 * atoms only. A query parameter occurring at several positions must match a service
 * parameter of the same type at each position.
 *
 * Several services may match when a query output parameter type is NONE (untyped).
 * There can be at most one matching service for each relation, so each service is
 * identified by the type signature of the corresponding relations.
 * 
 * The excludedSignatures array holds type signatures to exclude; a candidate service
 * with one of these signatures is skipped. Setting nExcluded = 0 excludes nothing.
 *
 * *hasNextMatch is set to true if a match outside the exclusion list exists beyond the one
 * returned. Caller can pass hasNextMatch = 0 if only one match is required.
 *
 * With matchMode = DISPATCH_MATCH_EXACT, returns a service with exact matching signatures.
 * With matchMode = DISPATCH_MATCH_RELAXED, returns a service that matches all
 * input parameters and has as few output parameters as possible.
 */
#define DISPATCH_MATCH_EXACT		1
#define DISPATCH_MATCH_RELAXED		2

/**
 * CLAUDE: Whether a service parameter direction can serve a query parameter direction.
 * The two must agree, except that with DISPATCH_MATCH_RELAXED a service output also
 * serves a query input: the caller reads the service and keeps the tuples agreeing with
 * the atom it bound; see CreateFilterOperator().
 */
bool DispatchParameterIOMatch(byte queryIO, byte serviceIO, int matchMode);

 bool DispatchParameterizedQuery(
	Atom queryTermForm, Atom const queryParameters[], size8 nParameters, int matchMode,
	Service * service, index8 permutation[],
	TypeSignature const excludedSignatures[], size8 nExcluded, bool * hasNextMatch);


/**
 * Iterating over the services matching a query. A caller that wants every matching
 * service should use the iterator rather than calling DispatchParameterizedQuery() once per
 * match, which repeats the search from the start every time.
 */
typedef struct {
	Atom const * queryParameters;
	size8 nParameters;
	// Which services count as matching; see DISPATCH_MATCH_EXACT
	int matchMode;
	index8 * permutation;
	RelationIterator relationIterator;
	ServiceIterator serviceIterator;
	// whether serviceIterator is positioned within the services of a relation table
	bool inRelation;
	Service service;
#ifdef DEBUG
	// Relation of the previous match, kept to verify that one query never matches two
	// services of one relation; see ServiceRegistryAdd()
	Relation const * previousMatchRelation;
#endif
} DispatchIterator;

/**
 * Create an iterator over the services matching the given query.
 * The queryParameters array must contain AT_PARAMETER atoms only must remain valid until
 * the iterator is ended. 
 * The iterator is positioned before the first matching service, so
 * DispatchIteratorNext() must be called before DispatchIteratorPeekService().
 * The permutation array must hold at least nParameters elements,
 * and receives the argument permutation of the current match; see DispatchQuery().
 * The caller must call DispatchIteratorEnd() when done.
 * matchMode is the same as in DispatchParameterizedQuery()
 */
void DispatchIterate(
	Atom queryTermForm, Atom const queryParameters[], size8 nParameters, int matchMode,
	index8 permutation[], DispatchIterator * iterator);

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
