/**
 * High level interface to relation tables, independent of implementation.
 * A relation table is 1:1 with a (form, columns types) pair.
 * A relation table should register one or more services for querying
 * and optionally agents for modifying contents (if read/write).
 * 
 * NOTE: the form is currently always a predicate form, which means we
 * cannot have services for negated predicates like (! odd x). 
 * It's not clear to me yet if this is a major limitation.
 * 
 * NOTE: in the future, we want to be able to hot-load implementations
 * into a running atom process. This would involve loading code into
 * executable memory and registering services with the appropriate
 * callback pointers.
 */

#ifndef RELATION_TABLE_H
#define RELATION_TABLE_H

#include "kernel/service.h"

typedef struct s_RelationTable RelationTable;

/**
 * Description of an implementation provider, such as RelationBTree.
 * One provider may provide multiple relation tables, sharing the same
 * callbacks.
 */
typedef struct s_RelationTableProvider {

	/** 
	 * Create a new relation table, return its storage data structure; this will
	 * be assigned to the RelationTable.stroage field and passed to the function
	 * below.
	 */
	void * (*createTable)(size8 nColumns, byte const atomTypes[]);

	/**
	 * Add a tuple to the given table.
	 * The atom types are fixed, so providing an Atom array is sufficient.
	 * If idPosition is > 0 it indicates the 1-based position of an identified
	 * atom (the tuple is part of an ifact).
	 */
	byte (*addTuple)(void * storage, Atom const tuple[], uint8 idPosition);

	/**
	 * Remove a specific tuple from the underlying relation.
	 * The removeTuple() function may assume the tuple contains no identified atom.
	 */
	byte (*removeTuple)(void * storage, Atom const tuple[]);

	/**
	 * Remove all tuples containing idAtom in the idPosition column (1-based)
	 */
	void (*removeIFactTuples)(void * storage, Atom idAtom, uint8 idPosition);

	/**
	 * Return number of tuples in the relation table
	 */
	size32 (*numberOfTuples)(void * storage);

	/**
	 * Free a relation table. Typically deallocates the underlying data structures.
	 */
	void (*free)(void * storage);

} RelationTableProvider;

// result codes for addTuple()
#define TUPLE_ADDED			1
#define TUPLE_EXISTS		2

// result codes for removeTuple()
#define TUPLE_REMOVED		1
#define TUPLE_NOT_FOUND		2

/**
 * A relation table implementation record, identified by (form, atomTypes).
 * 
 * Each implementation must provide callbacks to support adding
 * and removing tuples.
 */
struct s_RelationTable {
	Atom form;
	size8 nColumns;
	byte * atomTypes;
	// provider may be 0 for computed relations
	RelationTableProvider * provider;
	void * storage;	// any implementation-dependent storage data
};

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
 * The tuple must not contain an identified atom.
 * Does not remove lookup entries; see RetractFact()
 */
byte RelationTableRemoveTuple(RelationTable const * table, Atom const tuple[]);


void RelationTableRemoveIFactTuples(RelationTable const * table, Atom idAtom, uint8 idPosition);

/**
 * Print out an entire relation table, for debugging
 * 
 * NOTE: this requires querying, should be part of a service provider?
 * Or should we treat queries without variables ("check tuple") differently,
 * as part of the table functionality?
 */
void RelationTableDump(RelationTable const * table);


#endif	// RELATION_TABLE_H
