
#ifndef RELATION_SIGNATURE_H
#define RELATION_SIGNATURE_H

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

/**
 * Return the number of (nonzero) atom types in the given TypeSignature.
 * This is the same as the arity of a relation with this signature.
 */
size8 TypeSignatureNAtomTypes(TypeSignature typeSignature);

/**
 * A RelationSignature is a pair (term form, type signature). Using a term form
 * allows registering a negated predicate like (! odd x) as a relation distinct
 * from the non-negated (odd x). 
 * A RelationSignature plus an IOSignature identifies a Service.
 */
typedef struct s_RelationSignature {
	Atom termForm;
	TypeSignature typeSignature;
} RelationSignature;

/**
 * Ordering of two relation signatures
 */
int8 CompareRelations(RelationSignature relation, RelationSignature relationOrKey);

/**
 * Test relation signatures for identity
 */
bool SameRelations(RelationSignature relation1, RelationSignature relation2);

/**
 * Test for a null relation, marking an absent value (no relation)
 */
bool IsNullRelation(RelationSignature signature);

/**
 * Compute the hash of a relation signature, on top of an initialHash
 */
data64 RelationHash(RelationSignature signature, data64 initialHash);


#endif	// RELATION_SIGNATURE_H
