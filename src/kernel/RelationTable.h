/**
 * A RelationTable represents the tuple storage of one Relation, implemented by a StorageProvider,
 * and provides the interface for mutating (writing to) the relation.
 * Reading from a relation is done by services; see ServiceRegistry.h.
 * Computed relations do not have a RelationTable.
 *
 * NOTE: in the future, we want to be able to hot-load implementations into a running atom
 * process. This would involve loading code into executable memory and registering services
 * with the appropriate callback pointers.
 */

#ifndef RELATION_TABLE_H
#define RELATION_TABLE_H

#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "storage/StorageProvider.h"


// result codes for addTuple()
#define TUPLE_ADDED			1
#define TUPLE_EXISTS		2

// result codes for removeTuple()
#define TUPLE_REMOVED		1
#define TUPLE_NOT_FOUND		2
#define TUPLE_PROTECTED		3


typedef struct s_RelationTable {
	// NOTE: the Relation points here is merely used to find the RelationTable.
	Relation const * relation;

	/*
	 * The order of index columns. The stored tuples will be ordered  lexicographically by
	 * indexColumns[0], ..., indexColumns[nColumns-1]. Hence, lookup should be fast when
	 * leading columns are specified in this order, while out-of-order
	 * columns may lead to table scanning.
	 * For example, a relation with canonical order (element list position) and
	 * indexColumns = {1, 2, 0} will be ordered first by list, then by position, then by element;
	 * queries (@list _ _) and (@list @position _) should be fast, but (_ _ @element) may be slow.
	 */
	index8 indexColumns[RELATION_MAX_ARITY];

	StorageProvider const * provider;
	void * storage;			// implementation-dependent data, allocated by the StorageProvider

	size32 referenceCount;
} RelationTable;


/**
 * Create a relation table for the given Relation and record it in the relation table registry.
 * The RelationTable acquires the given Relation.
 * 
 * Storage for the new RelationTable is created using the specified storage provider,
 * which also provides operators for primitive services.
 * 
 * The caller acquires a reference to the returned RelationTable.
 *
 * The indexColumns array indicates the desired order of the index columns; see
 * RelationTable.indexColumns. Passing 0 gives the identity order.
 */
RelationTable * CreateRelationTable(
	Relation const * relation, StorageProvider const * provider, index8 const indexColumns[]);

/**
 * Find a relation table, or create one with the given storage provider if it does not exist.
 * If created, the table's indexColumns will be set to 0 (identity order).
 * The caller obtains a reference to the table in either case.
 */
RelationTable * FindOrCreateRelationTable(Relation const * relation, StorageProvider const * provider);

/**
 * Acquire a reference to a relation table.
 */
void AcquireRelationTable(RelationTable * table);

/**
 * Remove one reference to a relation table, and call CheckRelationTable()
 * to remove the table if this causes it to become stale.
 * A caller can release its reference to a table to render it "transient",
 * so that it will automatically be removed when no longer needed.
 */
void ReleaseRelationTable(RelationTable * table);

/**
 * Check whether a relation table is stale, and if so remove it.
 * A relation table is stale if
 *   (1) it has zero references,
 *   (2) it contains zero rows, and
 *   (3) no service depends on any of its primitive services.
 * Only certain kernel functions need to call this function.
 */
void CheckRelationTable(RelationTable * table);

/**
 * Return the number of rows in a relation table
 */
size32 RelationTableNRows(RelationTable const * table);

/**
 * Add a single tuple to the relation, acquiring each atom in the tuple.
 * If idPosition is > 0 it indicates the 1-based position of an identified atom.
 * Acquires a reference to each atom in the tuple, except an identified atom.
 * Does not add lookup entries; see AssertFact()
 */
byte RelationTableAddTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition);

/**
 * Remove the given tuple from the relation table.
 * If the tuple contains an identified atom, its position must match the given idPosition
 * to remove the tuple.
 * Does not remove lookup entries; see RetractFact()
 */
byte RelationTableRemoveTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition);

/**
 * Setup an empty relation table registry. Called during kernel bootstrapping only.
 */
void SetupRelationTableRegistry(void);

/**
 * The table storing the tuples of the given relation, or 0 if the relation is computed.
 */
RelationTable * FindRelationTable(Relation const * relation);

/**
 * Deallocate the registry. Before calling this function, all tables must have been dropped.
 */
void FreeRelationTableRegistry(void);

/**
 * Number of registered relation tables.
 */
size32 NumberOfRelationTables(void);


#endif	// RELATION_TABLE_H
