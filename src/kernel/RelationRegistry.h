/**
 * The relation registry keeps track of all registered relations, identified by their
 * (term form, column types) pair. Since a term form carries a sign, a predicate and its
 * negation are registered as two separate relations.
 *
 * The registry interns relations: one signature is one Relation record, so pointer
 * equality is signature equality, and the service registry and the relation table registry
 * both key on the pointer. It holds no reference of its own; a relation adds and removes
 * itself as it is created and released. See Relation.h
 */

#ifndef RELATION_REGISTRY_H
#define RELATION_REGISTRY_H

#include "btree/btree.h"
#include "kernel/Relation.h"


/**
 * Setup an empty relation registry. Called during bootstrapping only.
 */
void SetupRelationRegistry(void);

/**
 * Add a relation to the registry. Called by CreateRelation() only.
 */
void RelationRegistryAdd(Relation const * relation);

/**
 * Remove a relation from the registry. Called by ReleaseRelation() only, when the last
 * reference to the relation goes.
 */
void RelationRegistryRemove(Relation const * relation);

/**
 * Locate the relation for given (term form, column types), or 0 if there is none.
 */
Relation const * RelationRegistryFind(Atom form, size8 nColumns, byte const atomTypes[]);

/**
 * Deallocate the registry. Before calling this function,
 * all relations must have been released.
 */
void FreeRelationRegistry(void);

/**
 * Number of registered relations.
 */
size32 RelationRegistryNRelations(void);


/**
 * Iterating over the relations of a given term form.
 * A single term form may have several relations, one per combination of column types.
 */
typedef struct {
	Atom form;
	BTreeIterator btreeIterator;
} RelationIterator;

/**
 * Create an iterator over all relations registered for the given term form.
 * The iterator is positioned before the first matching relation, so
 * RelationIteratorNext() must be called before RelationIteratorGet().
 */
void RelationRegistryIterate(Atom form, RelationIterator * iterator);

/**
 * Advance to the next relation of the term form, if one exists.
 */
bool RelationIteratorNext(RelationIterator * iterator);

/**
 * The relation at the current iterator position.
 * Only valid after RelationIteratorNext() has returned true,
 * and until RelationIteratorEnd() is called.
 */
Relation const * RelationIteratorGet(RelationIterator const * iterator);

void RelationIteratorEnd(RelationIterator * iterator);


#endif  // RELATION_REGISTRY_H
