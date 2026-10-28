/**
 * A ServiceIterator iterates over facts from one service.
 * For a relation whose form repeats a role, ServiceIterator keeps only the canonical
 * tuples, where values of each exchangeable group ascend.
 * The kept tuples are in the order the operator yields them.
 */

#ifndef SERVICE_ITERATOR_H
#define SERVICE_ITERATOR_H

#include "kernel/operator.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"


typedef struct {
	ServiceRecord const * serviceRecord;
	// The argument of the service operator taken by each column; see
	// EqualitySignatureGetArgumentMap()
	index8 argumentMap[RELATION_MAX_ARITY];
	Atom arguments[RELATION_MAX_ARITY];
	// The exchangeable groups of the service; see ServiceGetExchangeableGroups()
	index8 groupArguments[RELATION_MAX_ARITY];
	size8 groupSizes[RELATION_MAX_ARITY];
	size8 nGroups;
	OperatorContext * context;
} ServiceIterator;


/**
 * Initialize a ServiceIterator. The arguments array must hold values for
 * all service inputs; the value of a service output is ignored.
 * The iterator will be positioned before the first tuple.
 * The service record must remain valid until ServiceIteratorEnd().
 */
void ServiceIterate(ServiceRecord const * serviceRecord, Atom const arguments[], ServiceIterator * iterator);

/**
 * Advance to the next tuple of the service, if one exists.
 * Once this function returns false, only ServiceIteratorEnd() may be called.
 */
bool ServiceIteratorNext(ServiceIterator * iterator);

/**
 * The value at the given column of the current tuple, with the atom type of that
 * column. Only valid after ServiceIteratorNext() has returned true.
 */
TypedAtom ServiceIteratorGetElement(ServiceIterator const * iterator, index8 column);

/**
 * Free the iterator's operator context. May be called at any position.
 */
void ServiceIteratorEnd(ServiceIterator * iterator);


#endif	// SERVICE_ITERATOR_H
