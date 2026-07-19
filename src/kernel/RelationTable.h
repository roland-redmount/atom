/**
 * High level interface to relation tables, independent of implementation.
 * A relation table is 1:1 with a (form, columns types) pair.
 * A relation table should register one or more services for querying
 * and optionally agents for modifying contents (if read/write).
 * 
 * NOTE: in the future, we want to be able to hot-load implementations
 * into a running atom process. This would involve loading code into
 * executable memory and registering services with the appropriate
 * callback pointers.
 */

#ifndef RELATION_TABLE_H
#define RELATION_TABLE_H

#include "lang/Atom.h"

typedef struct s_RelationTable RelationTable;

/**
 * Description of an implementation provider, such as RelationBTree.
 * One provider may provide multiple relation tables, sharing the same
 * callbacks.
 */
typedef struct s_RelationTableProvider {

	/** 
	 * Create a new relation table, return its data structure; this will
	 * be assigned to the RelationTable.data field.
	 * This function should also register services associated with the table
	 * in the service registry.
	 */
	void * (*createTable)(Atom form, size8 nColumns, byte const atomTypes[]);

	/**
	 * Add a tuple to the given table.
	 * The atom types are fixed, so providing an Atom array is sufficient.
	 * If idPosition is > 0 it indicates the 1-based position of an identified
	 * atom (the tuple is part of an ifact).
	 */
	byte (*addTuple)(RelationTable * table, Atom const tuple[], uint8 idPosition);

	/**
	 * Remove a tuple from the underlying relation.
	 */
	void (*removeTuple)(RelationTable * table, Atom const tuple[]);

	/**
	 * Return number of tuples in the relation table
	 */
	size32 (*numberOfTuples)(RelationTable * table);

	/**
	 * Free a relation table. Typically deallocates the underlying data structures.
	 */
	void (*free)(RelationTable * table);

} RelationTableProvider;

// result codes for addTuple()
#define TUPLE_ADDED			1
#define TUPLE_EXISTS		2

/**
 * A relation table implementation record.
 * Each implementation must provide callbacks to support adding
 * and removing tuples
 */
struct s_RelationTable {
	Atom form;
	size8 nColumns;
	byte * atomTypes;
	RelationTableProvider * provider;
	void * data;	// any implementation-dependent data
};

/**
 * Create a new relation table. The RelationTableProvider * must be valid for as
 * long as the implementation is in use. Calls provider->createTable().
 */
RelationTable CreateRelationTable(RelationTableProvider * provider, Atom form, size8 nColumns, byte const atomTypes[]);

/**
 * Locate a relation table for given (form, column types).
 * NOTE: if we store RelationTable in a B-tree we need to return a copy
 * of the structure. Same problem as for ServiceRecord. We might want
 * a "T-tree" structure where the data items are stored at stable addresses?
 */
RelationTable FindRelationTable(Atom form, byte const * atomTypes);


size32 RelationTableNRows(RelationTable const * table);


/**
 * Add a single tuple to the relation, acquiring each atom in the tuple.
 * Acquires a reference to each atom in the tuple.
 * Does not add entries to lookup; see AssertFact()
 */
byte RelationTableAddTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition);


/**
 * Remove the given tuple from the relation table.
 * Does not remove entries from the lookup table; see RetractFact()
 * If idPosition is > 0 it indicates the 1-based position of an identified
 * atom to be removed; if 0, a tuple containing an identified atom will not be removed.
 */
byte RelationTableRemoveTuple(RelationTable const * table, Atom const tuple[], uint8 idPosition);

/**
 * Print out an entire relation table, for debugging
 * 
 * NOTE: this requires querying, should be part of a service provider?
 * Or should we treat queries without variables ("check tuple") differently,
 * as part of the table functionality?
 */
// void RelationTableDump(RelationTable const * table);


#endif	// RELATION_TABLE_H
