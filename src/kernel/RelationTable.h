/**
 * A relation table is the tuple storage of one relation, and the interface for mutating
 * it. Reading a relation is the business of its services instead; see ServiceRegistry.h.
 *
 * A table is registered against a relation rather than pointed at by it, so that a
 * relation with no table registered is simply a computed relation; see Relation.h and
 * RelationTableRegistry.h.
 *
 * NOTE: in the future, we want to be able to hot-load implementations into a running atom
 * process. This would involve loading code into executable memory and registering services
 * with the appropriate callback pointers.
 */

#ifndef RELATION_TABLE_H
#define RELATION_TABLE_H

#include "kernel/operator.h"
#include "kernel/Relation.h"

typedef struct s_RelationTable RelationTable;

/**
 * Description of a storage implementation provider, such as RelationBTree.
 * One provider may provide the storage of many relations, sharing the same callbacks.
 *
 * Every hook receives the RelationTable, and so can read the column types and arity off
 * table->relation and the index column order off table->indexColumns. Only createStorage()
 * is called before table->storage is set.
 */
typedef struct s_RelationTableProvider {

	/**
	 * Create storage for a new relation table.
	 * The returned storage data pointer is assigned to the RelationTable.storage field,
	 * and so is not yet readable from the table when this is called.
	 * See also CreateRelationTable()
	 */
	void * (*createStorage)(RelationTable const * table);

	/**
	 * Register the services by which this table can be read, using
	 * RelationTableAddService().
	 *
	 * A provider registers exactly the services it can evaluate, and so this hook is
	 * where a provider declares what it is capable of: RelationBTree registers a service
	 * per prefix of the index column order, while an array-based provider might register
	 * a lookup by position and nothing else. A caller asking ServiceRegistryFind() for a
	 * signature no provider registered gets 0.
	 *
	 * Two services the rest of the kernel requires. A table must have the all-output
	 * service that enumerates every tuple, which RelationDump() uses. A table holding the
	 * tuples of identifying facts must additionally have the service taking the
	 * identified column as its only input; see IFactBeginConjunction().
	 */
	void (*registerServices)(RelationTable * table);

	/**
	 * Add a tuple to storage.
	 * The atom types are fixed, so providing an Atom array is sufficient.
	 * If idPosition is > 0 it indicates the 1-based position of an identified
	 * atom (the tuple is part of an ifact).
	 */
	byte (*addTuple)(RelationTable const * table, Atom const tuple[], uint8 idPosition);

	/**
	 * Remove a specific tuple from storage.
	 * If the stored tuple had an identified atom, it must match the given idPosition,
	 * or an error occurs.
	 */
	byte (*removeTuple)(RelationTable const * table, Atom const tuple[], uint8 idPosition);

	/**
	 * Return number of tuples in the relation table
	 */
	size32 (*numberOfTuples)(RelationTable const * table);

	/**
	 * Free the storage of a relation table, deallocating the underlying data structures.
	 * The table is empty by this point; see DropRelationTable().
	 */
	void (*free)(RelationTable const * table);

} RelationTableProvider;

// result codes for addTuple()
#define TUPLE_ADDED			1
#define TUPLE_EXISTS		2

// result codes for removeTuple()
#define TUPLE_REMOVED		1
#define TUPLE_NOT_FOUND		2
#define TUPLE_PROTECTED		3

/**
 * The tuple storage of one relation, held by a storage provider.
 *
 * A table is reference counted, because a machine operator reading it holds a pointer to
 * its storage and may outlive the table's registration: a rule that merely renames roles
 * compiles to a service evaluated by the very operator of the relation it reads, and that
 * service knows nothing of this table. The storage is deallocated once the last operator
 * reading it is gone, whatever order things are dropped in.
 */
struct s_RelationTable {
	// The relation whose tuples this table stores. Acquired, as the table may outlive
	// its registration.
	Relation const * relation;
	// Desired order of index columns, so that tuples are effectively ordered
	// lexicographically by indexColumns[0], ..., indexColumns[nColumns-1]. Hence, lookup
	// should be fast when leading columns are specified in this order, while out-of-order
	// columns may lead to table scanning. For example, a relation with canonical order
	// (element list position) and indexColumns = {1, 0, 2} will be ordered as
	// (list position element), so that queries (@list _ _) and (@list @position _) are
	// fast, but (_ _ @element) may be slow.
	index8 * indexColumns;
	RelationTableProvider const * provider;
	void * storage;	// any implementation-dependent storage data
	// One reference per machine operator reading this table, plus the creation reference
	// that DropRelationTable() releases.
	size32 referenceCount;
};

/**
 * Create tuple storage for the given relation using the specified provider, register it,
 * and let the provider register its services. Returns holding the creation reference,
 * which DropRelationTable() releases.
 *
 * The indexColumns array gives the desired order of the index columns; see
 * RelationTable.indexColumns. Passing 0 gives the identity order.
 *
 * The table acquires the relation, so the caller keeps its own reference.
 */
RelationTable * CreateRelationTable(
	Relation const * relation, RelationTableProvider const * provider,
	index8 const indexColumns[]);

/**
 * Register a primitive service of the relation this table stores, to be called from
 * provider->registerServices(). The service registry acquires the operator, so the caller
 * releases its own reference afterwards, as it would for ServiceRegistryAdd().
 */
void RelationTableAddService(RelationTable * table, byte const parameterIO[], Operator * op);

/**
 * Acquire a reference to a relation table.
 */
void AcquireRelationTable(RelationTable * table);

/**
 * Remove one reference to a relation table. When the last reference goes, the storage is
 * deallocated and the relation released.
 */
void ReleaseRelationTable(RelationTable * table);

/**
 * Remove the tuple storage of a relation: remove every service of the relation, unregister
 * the table and release the creation reference. The table must be empty.
 *
 * Every service goes, not only the ones this table's provider registered. A service
 * compiled against this relation answers as its facts stood, so removing the facts leaves
 * it stale; see ServiceRegistryRemoveAll().
 *
 * The storage itself is deallocated here only if no machine operator is still reading it;
 * see the note on reference counting on RelationTable.
 */
void DropRelationTable(RelationTable * table);

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

// NOTE: RelationDump() is declared in ServiceRegistry.h, since dumping the tuples of a
// relation requires a service to enumerate them.


#endif	// RELATION_TABLE_H
