/**
 * A term with untyped output parameters may dispatch to multiple services
 * with different type signatures. This occurs in compileTerm(), and can therefore
 * happen multiple times while compiling a clause. Each term dispatched during compilation
 * therefore creates a ChoicePoint, which may have 1 or more choices of type signatures.
 * The sequence of ChoicePoints encountered during compilation is a ChoiceTree.
 * (This is well defined because clauses are compiled in a deterministic order.)
 * Each combination of choices at each ChoicePoint yields one compiled service with a
 * specific signature. Typically, many choice points will have only one choice.
 *
 * We run the whole compilation once per combination, forcing a different choice each time.
 * In each run, we ask dispatch for a new matching service, besides those found so far;
 * see DispatchParameterizedQuery().
 */

#ifndef CHOICE_POINTS_H
#define CHOICE_POINTS_H

#include "kernel/Relation.h"		// for TypeSignature


// Most choice points one compilation may reach, which is one per term dispatched
#define MAX_CHOICE_POINTS	8

// Most alternatives one choice point may enumerate
#define MAX_CHOICE_POINT_MATCHES	8

/**
 * One dispatched term, and the choices made for it so far.
 */
typedef struct s_ChoicePoint {
	// Type signature of the service each choice dispatched to. These are the signatures
	// the next call to DispatchParameterizedQuery() excludes, so that it takes a match
	// this choice point has not taken yet.
	TypeSignature choiceSignatures[MAX_CHOICE_POINT_MATCHES];
	size8 nChoices;
	// whether a match outside choiceSignatures exists
	bool hasNextMatch;
#ifdef DEBUG
	// The form of the term dispatched here, kept to verify that every run reaches this
	// choice point with the same term
	Atom termForm;
#endif
} ChoicePoint;


/**
 * The choice points of one compilation, which is the path the current run takes through
 * the tree of combinations: one level per term dispatched, in the order the terms compile.
 * A run walks the path from the root, so the choice points beyond its depth are the ones
 * it has yet to reach.
 */
typedef struct s_ChoiceTree {
	ChoicePoint choicePoints[MAX_CHOICE_POINTS];
	// number of choice points the current run has reached
	index8 depth;
} ChoiceTree;


/**
 * Clear a choice tree.
 */
void ChoiceTreeReset(ChoiceTree * choiceTree);

/**
 * Advance a choice tree to the deepest choice point that still has a next (untried) match,
 * and reset the choice points below it. Returns false when no choice point has a next match.
 */
bool ChoiceTreeNextBranch(ChoiceTree * choiceTree);


#endif	// CHOICE_POINTS_H
