
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
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "lang/formula.h"


typedef enum e_DispatchResult {
	DISPATCH_FOUND = 1,				// Found a valid service
	DISPATCH_FOUND_STALE = 2,		// Found a service from a stale relation
	DISPATCH_NOT_FOUND = 3,
} DispatchResult;


/**
 * Dispatch a query, copying the first matching service to *service, if any.
 * The queryAtoms tuple must not contain parameters (AT_PARAMETER) atoms;
 * see DispatchParameterizedQuery().
 * If the query contains repeated variables, it will match only a Service
 * with the same repeated parameters.
 * The argument permutation required to match the service is written
 * to the given permutation array, such that queryActors element permutation[i]
 * matches service parameter i.
 * Returns true if a match was found.
 *
 * NOTE: this function yields a Service rather than the associated Operator,
 * since we often want to know the atom (column) types of the matched service.
 */
DispatchResult DispatchQuery(FormulaView query, Service * service, index8 permutation[]);

/**
 * Same, using a term (formula) instead of a termform and actors tuple
 */
DispatchResult DispatchQueryFormula(Atom queryTerm, Service * service, index8 permutation[]);

/**
 * A query (term) where actors have been replaced with a parameters tuple.
 */
/* CLAUDE: The form is a term form or a conjunction form; see IsRelationForm() */
typedef struct s_ParameterizedQuery {
	Atom form;
	Atom parameters[RELATION_MAX_ARITY];
	size8 arity;
} ParameterizedQuery;

void ParameterizeQuery(FormulaView query, ParameterizedQuery * parameterizedQuery);

void PrintParameterizedQuery(ParameterizedQuery const * parameterizedQuery);

/**
 * CLAUDE: Test whether two parameterized queries have the same form, the same type and IO
 * direction of each parameter, and repeat parameters at the same positions. The parameter
 * numbers may differ. Two such queries dispatch to the same services.
 */
bool SameParameterizedQueries(ParameterizedQuery const * query1, ParameterizedQuery const * query2);

/**
 * Dispatch a parameterized query. A query parameter occurring at several positions
 * must match a service parameter of the same type at each position.
 *
 * Several services may match when a query output parameter type is 0 (untyped).
 * There can be at most one matching service for each relation, so each service is
 * identified by the type signature of the corresponding relation.
 * 
 * The excludedSignatures array holds type signatures to exclude; a candidate service
 * with one of these signatures is skipped. Setting nExcluded = 0 excludes nothing.
 *
 * *hasNextMatch is set to true if a match outside the exclusion list exists beyond the one
 * returned. Caller can pass hasNextMatch = 0 if only one match is required.
 * TODO: can't we solve this "lookahead" problem more cleanly with DispatchIterate()
 *
 * matchMode is a combination of the flags below.
 */

// Return only services whose parameters exactly match the parameterized query.
#define DISPATCH_MATCH_EXACT		0

// Return a service whose input parameters all match an input parameter of the query,
// and that has as few output parameters as possible. For example, the parameterized query
// (#1<INT #2<INT #3>ID) will match service (#1<INT #2>INT #3>ID) with this flag set,
// but service (#1<INT #2>INT #3<ID) will not match.
// Services returned will generate additional tuples where the extra outputs do not match
// the query input, which must be removed using a FILTER operator.
#define DISPATCH_RELAX_IO			1

// Return a service that repeats a parameter where the query repeats it,
// but not necessarily everywhere the query repeats it.
// For example, service (#1 #2 #2) matches the parameterized query (#1 #1 #2)
// using DISPATCH_RELAX_EQUALITY
// Services returned will generate additional tuples whole columns differ where the query
// requires equality; these must be remove using a CONSTRAIN operator.
#define DISPATCH_RELAX_EQUALITY		2

DispatchResult DispatchParameterizedQuery(
	ParameterizedQuery const * query, int matchMode, Service * service, index8 permutation[],
	TypeSignature const excludedSignatures[], size8 nExcluded, bool * hasNextMatch);

/**
 * Test if a service parameter IO direction matches a query parameter IO direction.
 * With matchMode = DISPATCH_MATCH_EXACT, the two must agree; with DISPATCH_RELAX_IO
 * a service output also serves a query input.
 */
bool DispatchParameterIOMatch(byte queryIO, byte serviceIO, int matchMode);


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
	// The service record at the current positon
	ServiceRecord const * serviceRecord;
#ifdef DEBUG
	// Relation of the previous match, kept to verify that one query never matches two
	// services of one relation; see ServiceRegistryAdd()
	Relation const * previousMatchRelation;
#endif
} DispatchIterator;

/**
 * Create an iterator over the services matching the given query. Each service returned
 * by the iterator has a distinct type signature, belonging to a distinct relation.
 * The queryParameters array must contain AT_PARAMETER atoms only must remain valid until
 * DispatchIteratorEnd() is called.
 * The iterator is positioned before the first matching service, so
 * DispatchIteratorNext() must be called before DispatchIteratorPeekService().
 * The permutation array must hold at least nParameters elements,
 * and receives the argument permutation of the current match; see DispatchQuery().
 * The caller must call DispatchIteratorEnd() when done.
 * matchMode is the same as in DispatchParameterizedQuery().
 */
void DispatchIterate(
	ParameterizedQuery const * query, int matchMode, index8 permutation[], DispatchIterator * iterator);

/**
 * Advance to the next matching service, if one exists, writing its argument
 * permutation to the permutation array given to DispatchIterate().
 */
bool DispatchIteratorNext(DispatchIterator * iterator);

/**
 * View the service record for the service at the current iterator position.
 * Only valid after DispatchIteratorNext() has returned true, and until the next
 * call to DispatchIteratorNext() or DispatchIteratorEnd().
 */
ServiceRecord const * DispatchIteratorPeekServiceRecord(DispatchIterator const * iterator);

void DispatchIteratorEnd(DispatchIterator * iterator);


#endif	// DISPATCH_H
