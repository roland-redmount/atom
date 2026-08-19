/**
 * The relation table registry records where the tuples of a relation are stored, and is
 * how a signature reaches the storage to mutate. It is the write-side counterpart of the
 * service registry, which records how a relation is read; see ServiceRegistry.h.
 *
 * A relation with no table registered is a computed relation, which has services but no
 * tuples of its own. Nothing else records that: it is what
 * RelationTableRegistryFind() returning 0 means.
 *
 * The registry holds no reference to a table; a table adds and removes itself as it is
 * created and dropped. See RelationTable.h
 */

#ifndef RELATION_TABLE_REGISTRY_H
#define RELATION_TABLE_REGISTRY_H

#include "kernel/Relation.h"
#include "kernel/RelationTable.h"


/**
 * Setup an empty relation table registry. Called during bootstrapping only.
 */
void SetupRelationTableRegistry(void);

/**
 * Register the tuple storage of a relation. Called by CreateRelationTable() only.
 *
 * A relation may have at most one table. Lifting that restriction is what a relation with
 * several indexes would need here, along with a write path reaching every one of them.
 */
void RelationTableRegistryAdd(RelationTable const * table);

/**
 * Unregister the tuple storage of a relation. Called by DropRelationTable() only.
 */
void RelationTableRegistryRemove(RelationTable const * table);

/**
 * The table storing the tuples of the given relation, or 0 if the relation is computed.
 */
RelationTable * RelationTableRegistryFind(Relation const * relation);

/**
 * Deallocate the registry. Before calling this function, all tables must have been dropped.
 */
void FreeRelationTableRegistry(void);

/**
 * Number of registered relation tables.
 */
size32 RelationTableRegistryNTables(void);


#endif  // RELATION_TABLE_REGISTRY_H
