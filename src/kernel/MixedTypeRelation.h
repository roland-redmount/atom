/**
 * A mixed type relation is a transient relation answering one query, whose tuples may
 * have different atom types. A relation table fixes the atom type of every column, so a
 * query leaving an output untyped is answered by one service per type; see dispatch.h.
 * This type gathers those answers into one sequence of tuples, and drops the tuples the
 * query did not ask for.
 *
 * The tuples are yielded as TypedTuple, in the actor order of the query, each carrying
 * the column types of the service it came from. Two tuples of one mixed type relation
 * may therefore differ in their atom types, which is what the type is named for.
 *
 * A mixed type relation is its own iterator: it is created for one query, read once and
 * freed. It is not registered anywhere, unlike a Service.
 */

#ifndef MIXED_TYPE_RELATION_H
#define MIXED_TYPE_RELATION_H

#include "kernel/dispatch.h"
#include "kernel/typedtuple.h"


enum MixedTypeRelationType {
	/**
	 * CONCAT is the tuples of every service matching a query term, taken one service
	 * after another, restricted to the tuples satisfying the equality constraints of
	 * the query; see CreateConcatRelation().
	 */
	MIXED_TYPE_CONCAT = 1,

	/**
	 * FORMULA is the tuples satisfying a formula.
	 *
	 * NOTE: not implemented yet.
	 */
	MIXED_TYPE_FORMULA = 2,
};


typedef struct s_MixedTypeRelation {
	enum MixedTypeRelationType type;
	// Term form of the query this relation answers
	Atom termForm;
	// The tuple of the current iterator position, in query actor order.
	// Rewritten by every MixedTypeRelationNext().
	TypedTuple * tuple;
	union {
		// for MIXED_TYPE_CONCAT
		struct {
			TypedTuple const * queryActors;
			// The query generalized to parameters, which is what dispatch matches, and
			// which the dispatch iterator reads as it goes; see GetQueryParameters()
			Atom * queryParameters;
			// Index of the first query actor denoting the same variable as query actor i,
			// or just i when actor[i] is not a variable. Used to filter on equality constraints.
			// Set to 0 when the query actors contain no repeated variables (the common case)
			index8 * variableMap;
			// The arguments tuple the current service is called with, and the argument
			// permutation matching it, both in service parameter order
			Atom * arguments;
			index8 * permutation;
			DispatchIterator dispatchIterator;
			Service service;
			// Context of the current service, null before the first service and
			// between two services
			OperatorContext * context;
			// Set once the last matching service has been read, as neither the
			// dispatch iterator nor an operator context may be called again
			bool isExhausted;
		} concat;
		// for MIXED_TYPE_FORMULA
		struct {
			Atom formula;
		} formula;
	} impl;
} MixedTypeRelation;


/**
 * Create the relation of the tuples a query term asks for, gathered from every service
 * matching the query. A variable occurring at several positions of the query constrains
 * those positions to be equal, which dispatch does not match on, so a tuple failing that
 * constraint is dropped here; see DispatchQuery().
 *
 * The query actors tuple is not copied, and must remain valid until the relation is freed.
 * It holds the actors of a query and no parameter of its own, which DEBUG builds assert.
 * The tuple of a formula belongs to the formula registry and is shared by every holder of
 * that formula, so the caller must not write to it; see FormulaGetActors().
 *
 * NOTE: iteration keeps a DispatchIterator open, which write-locks the relation and
 * service registries. No service or relation may be registered while the tuples of this
 * relation are being read, so a query must be compiled before its answers are read.
 */
MixedTypeRelation * CreateConcatRelation(Atom queryTermForm, TypedTuple const * queryActors);

/**
 * Advance to the next tuple of the relation, if one exists. The relation is positioned
 * before its first tuple when created, so this function must be called before
 * MixedTypeRelationPeekTuple().
 */
bool MixedTypeRelationNext(MixedTypeRelation * relation);

/**
 * The tuple at the current position, in the actor order of the query.
 * Only valid after MixedTypeRelationNext() has returned true, and until the next call to
 * MixedTypeRelationNext() or FreeMixedTypeRelation().
 */
TypedTuple const * MixedTypeRelationPeekTuple(MixedTypeRelation const * relation);

/**
 * Deallocate a mixed type relation. May be called at any position, so a caller that has
 * seen the tuples it wanted need not read the relation to its end.
 */
void FreeMixedTypeRelation(MixedTypeRelation * relation);


#endif	// MIXED_TYPE_RELATION_H
