/**
 * A relation is the identity of a set of tuples: a term form together with a column
 * type per argument. It is interned in the relation registry, so that one signature is
 * one Relation record and pointer equality is signature equality.
 *
 * Using a term form (signed predicate) as key allows registering a negated predicate
 * like (! odd x) as a relation distinct from the non-negated (odd x).
 *
 * A relation names a signature and nothing else. Both the ability to read a relation and
 * the storage holding its tuples are registered against it, and neither is reachable from
 * the relation itself:
 *
 *   RelationRegistry       relations by (term form, column types); see RelationRegistry.h
 *   ServiceRegistry        how a relation can be read; see ServiceRegistry.h
 *   RelationTable          where its tuples are stored; see RelationTable.h
 *
 * A relation with no table registered is a computed relation: it has services, but no
 * tuples to mutate. This is the case of a machine service such as those in library/math.c,
 * and of a service the compiler produces.
 */

#ifndef RELATION_H
#define RELATION_H

#include "lang/Atom.h"


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


typedef struct s_Relation Relation;

struct s_Relation {
	// term form; the key this relation is registered under. See RelationRegistry.h
	Atom termForm;
	/**
	 * The predicate form of the term form, cached here because the roles of a
	 * relation are read on every tuple added or removed; see LookupAddPredicateRoles().
	 * Reading it off the term form instead would mean a relation query each time.
	 */
	Atom predicateForm;
	// whether this relation holds a reference to its forms; see RelationReleaseForm()
	bool ownsForm;
	size8 nColumns;
	TypeSignature typeSignature;
	/**
	 * One reference per Service and per RelationTable naming this relation, plus the
	 * creation reference held by whoever created it. The relation removes itself from the
	 * registry when the last reference is released; see ReleaseRelation().
	 *
	 * NOTE: mutable through a const pointer, as a reference count is not part of the
	 * value a relation denotes. Nearly everything holds a Relation const *.
	 */
	size32 referenceCount;
};


/**
 * Create a relation for the given signature and add it to the relation registry.
 * The relation must not already exist, or an ASSERT will occur.
 * The caller holds one reference to the relation.
 */
Relation const * CreateRelation(Atom termForm, size8 nColumns, TypeSignature typeSignature);

/**
 * Create a relation with the predicate form given explicitly, rather than computed from
 * TermFormGetPredicateForm(termForm). This function is only for bootstrapping, where
 * TermFormGetPredicateForm() is not yet available. See setupCoreServices() in kernel.c
 */
Relation const * CreateRelationBootstrap(
	Atom termForm, Atom predicateForm, size8 nColumns, TypeSignature typeSignature);

/**
 * The registered relation of the given signature, creating and registering one if there
 * is none. Unlike CreateRelation(), an existing relation is not an error.
 * The caller holds one reference to the relation either way.
 */
Relation const * FindOrCreateRelation(Atom termForm, size8 nColumns, TypeSignature typeSignature);

/**
 * Ordering of two relations
 */
int8 CompareRelations(Relation const * relation, Relation const * relationOrKey);

/**
 * Acquire a reference to a relation.
 */
void AcquireRelation(Relation const * relation);

/**
 * Remove one reference to the given relation.
 */
void ReleaseRelation(Relation const * relation);

/**
 * Release the references this relation holds to its term form and predicate form,
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
void RelationReleaseForm(Relation const * relation);

data64 RelationHash(Relation const * relation, data64 initialHash);

#endif	// RELATION_H
