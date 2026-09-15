/**
 * A RelationWriter is an interface to the tuple storage of a Relation,
 * providing the interface for mutating (writing to) the relation.
 * The storage mechanism is implemented by a StorageProvider.
 * Reading from a relation is done by services; see ServiceRegistry.h.
 * Computed relations do not have a RelationWriter.
 */

#ifndef RELATION_WRITER_H
#define RELATION_WRITER_H

#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "storage/StorageProvider.h"



typedef struct s_RelationWriter {
	RelationSignature relation;

} RelationWriter;


/**
 * Create a relation table for the given Relation and record it in the relation table registry.
 * The RelationWriter acquires the given Relation.
 * 
 * Storage for the new RelationWriter is created using the specified storage provider,
 * which also provides operators for primitive services.
 * 
 * The caller acquires a reference to the returned RelationWriter.
 *
 * The indexColumns array indicates the desired order of the index columns; see
 * RelationWriter.indexColumns. Passing 0 gives the identity order.
 *
 * The number of columns of the relation table is determied from its type
 * signature; see TypeSignatureNColumns().
 */
RelationWriter * CreateRelationWriter(
	RelationSignature relation, StorageProvider const * provider, index8 const indexColumns[]);

/**
 * Find a relation table, or create one with the given storage provider if it does not exist.
 * If created, the table's indexColumns will be set to 0 (identity order).
 * The caller obtains a reference to the table in either case.
 * 
 * NOTE: index columns belongs to the Relation, not the writer. Affects all searches, read and write.
 */
RelationWriter * FindOrCreateRelationTable(
	RelationSignature relation, StorageProvider const * provider);

/**
 * Remove a RelatonWriter. This should only be called when removing the Relation.
 */
void DropRelationWriter(RelationWriter * writer);


/**
 * Setup an empty relation table registry. Called during kernel bootstrapping only.
 */
void SetupRelationTableRegistry(void);

/**
 * The table storing the tuples of the given relation, or 0 if the relation is computed.
 */
RelationWriter * FindRelationTable(RelationSignature relation);

/**
 * Deallocate the registry. Before calling this function, all tables must have been dropped.
 */
void FreeRelationTableRegistry(void);

/**
 * Number of registered relation tables.
 */
size32 NumberOfRelationTables(void);


#endif	// RELATION_WRITER_H
