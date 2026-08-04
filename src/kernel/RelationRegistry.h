/**
 * The relation registry keeps track of all available relation tables,
 * identified by their (form, column types) pair.
 * Services for these relations are registered separately; see ServiceRegistry.h
 */

#ifndef RELATION_REGISTRY_H
#define RELATION_REGISTRY_H

#include "btree/btree.h"
#include "kernel/RelationTable.h"


/**
 * Setup an empty relation registry. Called during bootstrapping only.
 */
void SetupRelationRegistry(void);

/**
 * Add a relation table to the registry.
 * The registry takes ownership of the relation, and will call FreeRelationTable()
 * upon removal.
 */
void RelationRegistryAdd(RelationTable const * relation);

/**
 * Remove a relation table, include all stored tuples (if any).
 * Any associated services must have been removed first;
 * see RelationRemoveAllServices().
 * Calls FreeRelationTable()
 */
void RelationRegistryRemove(RelationTable const * relation);

/**
 * Locate a relation table for given (form, column types).
 */
RelationTable const * RelationRegistryFind(Atom form, size8 nColumns, byte const atomTypes[]);

/**
 * Deallocate the registry. Before calling this function,
 * all relation tables must have been removed.
 */
void FreeRelationRegistry(void);

/**
 * Number of registered relation tables.
 */
size32 RelationRegistryNTables(void);


#endif  // RELATION_REGISTRY_H
