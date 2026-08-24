/**
 * High-level user query interface
 */

#ifndef QUERY_H
#define QUERY_H

#include "kernel/MixedTypeRelation.h"
#include "lang/formula.h"


/**
 * Execute a query, returning a mixed-type relation containing every fact entailed by
 * the knowledge base for the given query term . The caller iterates the returned relation
 * and frees it with FreeMixedTypeRelation().
 * The query term must outlive the returned relation, which reads its actors.
 *
 * A query that does not dispatch to any service is compiled, registering new services.
 * Dispatch and the compiler both work with the parameterized query, so a query answered by
 * an earlier query that parameterizes the same way is not compiled again; see
 * FindOrCompileService() and GetQueryParameters().
 *
 * NOTE: a parameterized query that compiles to nothing is compiled again every time it is
 * asked. Compiling nothing registers nothing, so this costs a walk over the rules and
 * no more, and it lets a query start working once a rule that answers it is asserted.
 *
 * NOTE: a compiled service is a cache over the knowledge base, and is removed again when
 * a change to the facts or the rules could alter what it yields, so that the next query
 * parameterizing the same way compiles it anew; see ServiceRegistryInvalidateTermForm().
 */
MixedTypeRelation * UserQuery(Atom queryTerm);


#endif	// QUERY_H
