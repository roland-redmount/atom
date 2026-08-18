/**
 * The user query interface: the layer above dispatch and the compiler that answers a
 * query as a user expects it to be answered, without the user knowing which services
 * exist. See compiler.h for what compilation does, and MixedTypeRelation.h for how the
 * tuples of several services become one relation.
 */

#ifndef QUERY_H
#define QUERY_H

#include "kernel/MixedTypeRelation.h"
#include "lang/Formula.h"


/**
 * Answer a user query: the relation of every fact the knowledge base entails for the
 * given query term, whether stored as a fact or derived through a rule. The caller
 * iterates the returned relation and frees it with FreeMixedTypeRelation().
 * The query term must outlive the returned relation, which reads its actors.
 *
 * A query asked for the first time is compiled here, which registers the services
 * answering it; a query asked again is answered by those services. Whether a query has
 * been asked before is decided by dispatching its type, which is the query generalized
 * to parameters; see GetQueryParameters(). Dispatching the actors instead would ask a
 * narrower question and lead to compiling a service that already exists: the query
 * (list "ab" position x element x) matches no service, its repeated variable spanning
 * columns of two types, while its type (list <ID position >UINT element >LETTER) is a
 * service of the kernel.
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
