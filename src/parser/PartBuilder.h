

#ifndef PARTBUILDER_H
#define PARTBUILDER_H


#include "parser/Token.h"


struct s_FormulaBuilder;


/**
 * A part is a role name followed by an actor. The actor may be written as a
 * reflection [ ... ], in which case the part builder collects the tokens of
 * the reflection in a nested formula builder while in STATE_REFLECTION, and
 * the formula it yields becomes the actor.
 *
 * The nested builder is held by pointer and allocated only when a reflection
 * begins. A FormulaBuilder contains a PartBuilder in turn, so holding one by
 * value would make this structure infinitely recursive.
 */
typedef struct s_PartBuilder {
	enum BuilderState {
		STATE_EMPTY, STATE_HAS_NAME, STATE_REFLECTION, STATE_COMPLETE
	} state;
	Atom role;
	TypedAtom actor;
	struct s_FormulaBuilder * formulaBuilder;	// only in STATE_REFLECTION
} PartBuilder;


void InitializePartBuilder(PartBuilder * builder);

bool PartBuilderPush(PartBuilder * builder, Token token);

bool PartBuilderIsEmpty(PartBuilder const * builder);

bool PartBuilderComplete(PartBuilder const * builder);

/**
 * Return the role name (AT_NAME)
 */
Atom PartBuilderGetRole(PartBuilder const * builder);

/**
 * Return the actor (any atom type)
 */
TypedAtom PartBuilderGetActor(PartBuilder const * builder);

void PartBuilderReset(PartBuilder * builder);


#endif	// PARTBUILDER_H
