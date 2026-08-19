/**
 * High-level user query interface
 */

#ifndef QUERY_H
#define QUERY_H

#include "kernel/MixedTypeRelation.h"
#include "lang/Formula.h"


/**
 * Execute a query, returning a mixed-type relation containing every fact entailed by
 * the knowledge base for the given query term . The caller iterates the returned relation
 * and frees it with FreeMixedTypeRelation().
 * The query term must outlive the returned relation, which reads its actors.
 *
 * A query that does not dispatch to any service is compiled, registering new services.
 * Dispatch and the compiler both work by the query type, so a query answered by an
 * earlier query of its type is not compiled again; see DispatchQuery() and
 * GetQueryParameters(). What the type leaves out is the equality constraint of a repeated
 * actor, which the returned relation applies to the tuples it reads, so a query such as
 * (item z index z) is answered from the service compiled for (item e index p); see
 * testQueryTypeIgnoresRepeatedVariable().
 *
 * NOTE: a query whose type compiles to nothing is compiled again every time it is
 * asked. Compiling nothing registers nothing, so this costs a walk over the rules and
 * no more, and it lets a query start working once a rule that answers it is asserted.
 *
 * NOTE: a compiled service is a cache over the knowledge base, and is removed again when
 * a change to the facts or the rules could alter what it yields, so that the next query
 * of its type compiles it anew; see ServiceRegistryInvalidateTermForm().
 */
MixedTypeRelation * UserQuery(Formula const * queryTerm);


#endif	// QUERY_H
