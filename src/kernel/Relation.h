/**
 * A Relation encapsulates a relation (a set of tuples), and is identified by
 * a Relation. A Relaton may have one RelationWriter, and one or more Services.
 * 
 * A Relation for which no RelationWriter exists is read-only. Examples arithmetic relations,
 * and relations (and their services) produced by the compiler.
 * 
 * A Relation with a RelationWriter but no services write-only, a kind of data sink.
 * Output devices such as a screen canvas can be modelled as write-only relations.
 */

#ifndef RELATION_H
#define RELATION_H

// #include "kernel/ServiceRegistry.h"
#include "btree/btree.h"
#include "lang/Atom.h"

struct s_TupleStore;


// We limit the number of arguments a relation might have,
// so that we can use fixed-size arrays in some places and avoid heap allocation.
// In practice, services should rarely have arity higher than 3.
#define RELATION_MAX_ARITY	8


/**
 * The column types of a relation. This exists to simplify array handling.
 */
typedef struct s_TypeSignature {
	byte atomTypes[RELATION_MAX_ARITY];
} TypeSignature;

/**
 * The type signature of the given atom types, zero filled beyond nColumns. For a signature
 * built from an array at hand, such as the atom types of a TypedTuple.
 */
TypeSignature CreateTypeSignature(byte const atomTypes[], size8 nColumns);


bool SameTypeSignatures(TypeSignature signature1, TypeSignature signature2);

/**
 * Return the number of (nonzero) atom types in the given TypeSignature.
 * This is the same as the arity of a relation with this signature.
 */
size8 TypeSignatureNAtomTypes(TypeSignature typeSignature);

/**
 * A Relation is a pair (term form, type signature). Using a term form
 * allows registering a negated predicate like (! odd x) as a relation distinct
 * from the non-negated (odd x). 
 * A Relation plus an IOSignature identifies a Service.
 */
typedef struct s_Relation {
	Atom termForm;
	TypeSignature typeSignature;
} Relation;

/**
 * Ordering of two relation signatures
 */
int8 CompareRelations(Relation relation, Relation relationOrKey);

/**
 * Test relation signatures for identity
 */
bool SameRelations(Relation relation1, Relation relation2);

/**
 * Test for a null relation, marking an absent value (no relation)
 */
bool IsNullRelation(Relation signature);

/**
 * Compute the hash of a relation signature, on top of an initialHash
 */
data64 RelationHash(Relation signature, data64 initialHash);


/**
 * Register a new Relation, or aquire a reference to one that already exists.
 */
void AcquireRelation(Relation relation);

/**
 * Release a reference to a relation.
 */
void ReleaseRelation(Relation relation);

/**
 * Create a relation with the predicate form given explicitly, rather than computed from
 * TermFormGetPredicateForm(termForm). This function is only for bootstrapping, where
 * TermFormGetPredicateForm() is not yet available. See setupCoreServices() in kernel.c
 */
void CreateRelationBootstrap(Relation relation, Atom predicateForm);

/**
 * Create a relation from a term, whose actors must be parameters, e.g.
 * for example (+ @1<INT + @2<INT = @3>INT)
 * The IO direction of each parameter is ignored.
 */
Relation CreateRelationFromTerm(Atom term);

/**
 * Set the relation's tuple store. This should only be called from CreateTupleStore().
 */
// void RelationSetTupleStore(Relation relation, struct s_TupleStore * store);

/**
 * Return the TupleStore associated with this Relation, or 0 if none exists.
 */
struct s_TupleStore * RelationGetTupleStore(Relation relation);


/**
 * Add a primitive service to the relation, specified by a RelationReader.
 * This is a low-level method, should only be called by the storage provider.
 */
// Service RelationAddPrimitiveService(Relation relation, RelationReaderSpec const * readerSpec);

/**
 * Return the predicate form corresponding to the Relation's term form.
 * This is used to avoid calling TermFormGetPredicateForm() form LookupAddPredicateRoles(),
 * which is critical during bootstrap; see CreateRelationBootstrap()
 */
Atom RelationGetPredicateForm(Relation relation);

/**
 * Test if the given relation exists in the registry.
 */
bool RelationExists(Relation signature);

/**
 * Return the number of columns in a relation.
 */
// size32 RelationNColumns(Relation signature);

/**
 * Return the number of rows in a relation.
 */
size32 RelationNRows(Relation signature);

/**
 * Add a single tuple to the relation, acquiring each atom in the tuple.
 * If idPosition is > 0 it indicates the 1-based position of an identified atom.
 * Acquires a reference to each atom in the tuple, except an identified atom.
 * Does not add lookup entries; see AssertFact()
 */
byte RelationAddTuple(Relation signature, Atom const tuple[], uint8 idPosition);

/**
 * Remove the given tuple from the relation.
 * If the tuple contains an identified atom, its position must match the given idPosition
 * to remove the tuple.
 * Does not remove the associated lookup entries; see RetractFact()
 */
byte RelationRemoveTuple(Relation signature, Atom const tuple[], uint8 idPosition);

/**
 * Drop the relation, all its services, and any associated tuple storage.
 */
void DropRelation(Relation signature);

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
void RelationReleaseTermForm(Relation signature);

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
Relation RelationIteratorGet(RelationIterator const * iterator);


void RelationIteratorEnd(RelationIterator * iterator);


#endif	// RELATION_H
