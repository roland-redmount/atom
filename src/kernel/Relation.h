/**
 * A Relation is a term form together with a column type per argument. It is the identifier
 * for a RelationTable. A Relation plus an IOSignature identifies a Service.
 * Using a term form (signed predicate) as key allows registering a negated predicate
 * like (! odd x) as a relation distinct from the non-negated (odd x).
 *
 * A Relation for which no RelationTable exists is a computed relation: it has services,
 * but no mutable facts. Examples are machine service such as those in library/math.c,
 * and services produced by the compiler.
 */

#ifndef RELATION_H
#define RELATION_H

#include "lang/Atom.h"
#include "btree/btree.h"


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


typedef struct s_Relation {
	Atom termForm;
	TypeSignature typeSignature;
} Relation;

/**
 * Create a Relation. The caller holds one reference to the relation,
 * which must be released when no longer needed.
 * 
 * NOTE: an alternative is void AddRelation(Relation relation) where
 * the caller creates the struct (Relation) {termForm, typeSignature}.
 * Perhaps more transparent -- this function doesn't create a Relation
 * so much as add it to the registry
 */
Relation CreateRelation(Atom termForm, TypeSignature typeSignature);

/**
 * Create a relation with the predicate form given explicitly, rather than computed from
 * TermFormGetPredicateForm(termForm). This function is only for bootstrapping, where
 * TermFormGetPredicateForm() is not yet available. See setupCoreServices() in kernel.c
 */
Relation CreateRelationBootstrap(Atom termForm, Atom predicateForm, TypeSignature typeSignature);

/**
 * Return the predicate form corresponding to the Relation's term form.
 * This is used to avoid calling TermFormGetPredicateForm() form LookupAddPredicateRoles(),
 * which is critical during bootstrap; see CreateRelationBootstrap()
 */
Atom RelationGetPredicateForm(Relation relation);


bool RelationExists(Relation relation);

/**
 * Ordering of two relations
 */
int8 CompareRelations(Relation relation, Relation relationOrKey);

bool SameRelations(Relation relation1, Relation relation2);

/**
 * Acquire a reference to a relation.
 */
void AcquireRelation(Relation relation);

/**
 * Remove one reference to the given relation.
 */
void ReleaseRelation(Relation relation);

/**
 * Test for a null relation, marking an absent value (no relation)
 */
bool IsNullRelation(Relation relation);

/**
 * CLAUDE: Release the references this relation holds to its term form and predicate form,
 * without releasing the relation.
 *
 * This is only for shutting down the self-referential core relations, whose own defining
 * facts are stored in their own tables. Such a relation cannot be released directly:
 * dropping its table requires it to be empty, but the tuples are only retracted once the
 * form's reference count drops to zero, which cannot happen while the relation holds a
 * reference. Detaching the references first lets the ifact drain its tuples out of a
 * relation that is still registered and still serviced.
 *
 * The term form is released before the predicate form, since the defining fact of the
 * term form holds a reference to the predicate form.
 *
 * The relation must still be registered (its form is the registry B-tree key) and must
 * still have its services, which are used to locate the tuples to retract. It should be
 * released immediately afterwards.
 */
void RelationReleaseForms(Relation relation);

/**
 * Compute the hash of a relation, on top of an initialHash
 */
data64 RelationHash(Relation relation, data64 initialHash);

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
