/**
 * Storage for unique typed tuples. Used to build predicates for
 * queries, rules and other situations where the predicate is not
 * an asserted fact and so cannot be stored in a relation table.
 * Tuples are hashed, but are not language elements; there is no
 * "reflected tuple" atom.
 * 
 * NOTE: this is redundant with relation tables in the sense that
 * a tuple might be stored in both places. The tuple hash is central,
 * as it is needed to define a reflected predicate (and other formulas).
 * But as long as we compute the tuple hash in the same way, we may 
 * store the tuple in any way; as a TypedTuple, or as a row of a relation
 * table (where types are stored separately).  
 */

 #ifndef TUPLESTORE_H
 #define TUPLESTORE_H

#include "kernel/typedtuple.h"


// Storage structure, can be private?
typedef struct s_TupleRecord {
	data64 hash;
	TypedTuple * tuple;
} TupleRecord;

/**
 * Store the given tuple in the store, assuming ownership.
 * If the tuple already exists, the given tuple is deallocated.
 * Returns its hash value. The caller obtains a reference to
 * the stored tuple.
 */
data64 TupleStoreInsert(TypedTuple const * tuple);

/**
 * View a stored tuple by hash value
 */
TypedTuple const * TupleStorePeek(data64 hash);

/**
 * Release a reference to a stored tuple
 */
void ReleaseTuple(data64 hash);


#endif // TUPLESTORE_H
