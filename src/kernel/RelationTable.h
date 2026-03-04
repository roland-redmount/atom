/**
 * High level interface to relation tables, independent of implementation.
 * 
 * TODO: This is not in use yet.
 */

#ifndef RELATION_TABLE_H
#define RELATION_TABLE_H


// Relation table implementations

#define RELATION_BTREE			1		// RelationBTree.c
#define RELATION_VAR_ARRAY		2

// this overlaps with RegistryCreateTable(), RegistryDropTable()

// void CreateRelationTable(Atom form);
// void FreeRelationTable(RelationTable * table);

size8 RelationTableNColumns(Atom form);
size32 RelationTableNRows(Atom form);



typedef struct s_RelationTableIterator {
	// TODO
} RelationTableIterator;


/**
 * Initialize an iterator returning all tuples matching queryTuple,
 * or if queryTuple is 0, returning all tuples in the table.
 * After this call, the iterator will be positioned at the first tuple
 * matching the query, if any, and RelationTableIteratorHasTuple() may be called.
 * The table is locked to prevent modification while iterating.
 * 
 * NOTE: the iterator does not keep a copy of queryTuple,
 * so it must remain unchanged during the iteration.
 */
void RelationTableIterate(RelationTable const * table, Atom const * queryTuple, RelationTableIterator * iterator);

/**
 * Test if the iterator has a next element.
 * If this function returns true, the tuple can be accessed by 
 * RelationTableIteratorGetTuple() or RelationTableIteratorGetAtom().
 */
bool RelationTableIteratorHasTuple(RelationTableIterator const * iterator);

/**
 * Advance the iterator to the next tuple matching the query, if any
 */
void RelationTableIteratorNext(RelationTableIterator * iterator);

Atom RelationTableIteratorGetAtom(RelationTableIterator const * iterator, index8 i);

/**
 * Return a pointer to the current tuple.
 */
Atom const * RelationTableIteratorGetTuple(RelationTableIterator const * iterator);

/**
 * Terminate the iterator.
 */
void RelationTableIteratorEnd(RelationTableIterator * iterator);

/**
 * Query the relation and return a single tuple.
 * The relation table must have exactly one tuple matching the query.
 */
Atom const * RelationTableQuerySingle(RelationTable const * table, Atom const * queryTuple);


/**
 * Add a aingle tuple to the relation, acquiring each atom in the tuple.
 * Does not add entries to lookup; see AssertFact()
 */
byte RelationTableAddTuple(RelationTable * table, Atom const * tuple);


// result codes for RelationTableAddTuple()
#define TUPLE_ADDED			1
#define TUPLE_EXISTS		2
#define TUPLE_PROTECTED		3	// adding would violate an ifact definition


/**
 * Remove tuples from the Table matching the query.
 * If queryTuple matches a tuple containing an atom with the ATOM_PROTECTED bit set,
 * it is not removed unless 
 */
size32 RelationTableRemoveTuples(RelationTable * table, Atom const * queryTuple, uint8 mode);

#define REMOVE_NORMAL		0
#define REMOVE_PROTECTED	1


/**
 * Print out an entire relation table, for debugging
 */
void RelationTableDump(RelationTable const * table);


#endif	// RELATION_TABLE_H
