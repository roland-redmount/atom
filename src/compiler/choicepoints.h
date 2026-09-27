/**
 * A term with untyped output parameters may dispatch to multiple services
 * with different type signatures. This occurs in compileTermSet(), and can therefore
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

#include "kernel/Relation.h"		// for RELATION_MAX_ARITY
#include "kernel/ServiceRegistry.h"	// for Service


// Most choice points one compilation may reach, which is one per term dispatched
#define MAX_CHOICE_POINTS	8

// Most alternatives one choice point may enumerate
#define MAX_CHOICE_POINT_MATCHES	8

/**
 * A ChoicePoint represents one dispatched term or conjunction, with the choices made
 * (services found by dispatch) so far.
 */
typedef struct s_ChoicePoint {
	// The terms compiled at this choice point, as indices into the clause terms.
	// Every term has at least one actor, so a set of terms dispatched to one relation
	// has at most RELATION_MAX_ARITY terms.
	index8 termIndices[RELATION_MAX_ARITY];
	size8 nTerms;
	// The index into the clause actors of each actor of the compiled terms (above).
	// The permutation[] array refers to this array.
	index8 actorIndices[RELATION_MAX_ARITY];
	size8 nActors;
	// The service dispatched to by each choice. The relations of these services
	// are excluded when dispatching for a new choice. A recursive term compiles without
	// dispatch, and has no choices.
	Service choices[MAX_CHOICE_POINT_MATCHES];
	size8 nChoices;
	// The argument permutation obtained from dispatch for the current Service choice
	// = choices[nChoices - 1]. Indexes into the actorIndices[] array.
	index8 permutation[RELATION_MAX_ARITY];
	// whether another matching service exists, in addition to the above
	bool hasNextMatch;
} ChoicePoint;


/**
 * A ChoiceTree holds the choice points for the compilation of one conjunction (a "run").
 * Choice point k holds the k'th compiled term. A run first compiles the terms of
 * the fixed choice points, in order; each re-uses the current chosen Service, except the
 * last one, which takes a new choice. The run then continues with choice points of its own.
 */
typedef struct s_ChoiceTree {
	ChoicePoint choicePoints[MAX_CHOICE_POINTS];
	// CLAUDE: Number of choice points recorded by the current run, one per compiled term.
	// After the run, ChoiceTreeNextBranch() looks for a branch among these.
	size8 nChoicePoints;
	// CLAUDE: The choice point where the current run takes a new choice, or NO_BRANCH in the
	// first run. The choice points before it repeat their current choice from the previous
	// run; see ChoiceTreeNextBranch().
	index8 branchIndex;
} ChoiceTree;

// CLAUDE: ChoiceTree.branchIndex of the first run, which searches every choice point afresh
#define NO_BRANCH	255


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
