/**
 * A Relation encapsulates a relation (a set of tuples), and is identified by
 * a RelationSignature. A Relaton may have one RelationWriter, and one or more Services.
 * 
 * A Relation for which no RelationWriter exists is read-only. Examples arithmetic relations,
 * and relations (and their services) produced by the compiler.
 * 
 * A Relation with a RelationWriter but no services write-only, a kind of data sink.
 * Output devices such as a screen canvas can be modelled as write-only relations.
 */

#ifndef RELATION_H
#define RELATION_H

#include "kernel/RelationSignature.h"
#include "kernel/ServiceRegistry.h"
#include "storage/StorageProvider.h"
#include "btree/btree.h"


/**
 * Create a Relation using the given provider. Creates storage and/or services
 * as specified by the provider. The relation must not already exist.
 * For relations that do not store data, set provider to 0 to obtain the default provider.
 * 
 * TODO: this should take a RelationSignature instead of (termForm, typeSignature)
 */
RelationSignature CreateRelation(
	Atom termForm, TypeSignature typeSignature, StorageProvider const * provider, index8 const indexColumns[]);

/**
 * Create a relation with the predicate form given explicitly, rather than computed from
 * TermFormGetPredicateForm(termForm). This function is only for bootstrapping, where
 * TermFormGetPredicateForm() is not yet available. See setupCoreServices() in kernel.c
 */
RelationSignature CreateRelationBootstrap(
	Atom termForm, Atom predicateForm, TypeSignature typeSignature,
	StorageProvider const * provider, index8 const indexColumns[]);

/**
 * Create a relation from a term, whose actors must be parameters, e.g.
 * for example (+ @1<INT + @2<INT = @3>INT)
 * The IO direction of each parameter is ignored.
 */
RelationSignature CreateRelationFromTerm(Atom term, StorageProvider const * provider);

/**
 * Add a primitive service to the relation, specified by a RelationReader.
 * This is a low-level method, should only be called by the storage provider.
 */
Service RelationAddPrimitiveService(RelationSignature relation, RelationReaderSpec const * readerSpec);

/**
 * Return the predicate form corresponding to the Relation's term form.
 * This is used to avoid calling TermFormGetPredicateForm() form LookupAddPredicateRoles(),
 * which is critical during bootstrap; see CreateRelationBootstrap()
 */
Atom RelationGetPredicateForm(RelationSignature relation);


bool RelationExists(RelationSignature signature);

/**
 * Return the number of columns in a relation.
 */
size32 RelationNColumns(RelationSignature signature);

/**
 * Return the number of rows in a relation.
 */
size32 RelationNRows(RelationSignature signature);

/**
 * Add a single tuple to the relation, acquiring each atom in the tuple.
 * If idPosition is > 0 it indicates the 1-based position of an identified atom.
 * Acquires a reference to each atom in the tuple, except an identified atom.
 * Does not add lookup entries; see AssertFact()
 */
byte RelationAddTuple(RelationSignature signature, Atom const tuple[], uint8 idPosition);

/**
 * Remove the given tuple from the relation.
 * If the tuple contains an identified atom, its position must match the given idPosition
 * to remove the tuple.
 * Does not remove the associated lookup entries; see RetractFact()
 */
byte RelationRemoveTuple(RelationSignature signature, Atom const tuple[], uint8 idPosition);

/**
 * Drop the relation, its storage, readers and writers.
 */
void DropRelation(RelationSignature signature);

/**
 * Release the references this relation holds to its term form, without releasing the relation.
 *
 * This is only used for teardown of the self-referential core relations, whose own defining
 * facts are stored in their own tables. Such a relation cannot be released directly:
 * dropping its table requires it to be empty, but the tuples are only retracted once the
 * form's reference count drops to zero, which cannot happen while the relation holds a
 * reference. Detaching the references first lets the ifact drain its tuples out of a
 * relation that is still registered and still serviced.
 *
 * The relation must still be registered (its form is the registry B-tree key) and must
 * still have its services, which are used to locate the tuples to retract. It should be
 * released immediately afterwards.
 */
void RelationReleaseTermForm(RelationSignature signature);

/**
 * Setup an empty relation registry. Called during bootstrapping only.
 */
void SetupRelationRegistry(void);

/**
 * Deallocate the registry. Before calling this function,
 * all relations must have been released.
 * TODO: rename SetupRelations() ?
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
RelationSignature RelationIteratorGet(RelationIterator const * iterator);


void RelationIteratorEnd(RelationIterator * iterator);


#endif	// RELATION_H
