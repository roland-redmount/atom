

#ifndef PARTBUILDER_H
#define PARTBUILDER_H


#include "parser/Token.h"


struct s_FormulaBuilder;


/**
 * A part is a role name followed by an actor. The actor may be written as a
 * reflection [ ... ], in which case the part builder collects the tokens of
 * the reflection in a nested formula builder while in STATE_REFLECTION, and
 * the formula atom it yields becomes the actor.
 *
 * A part builder holds the same alternation as the tokenizer, which reads an actor after
 * a role name; see enum TokenizerState. It therefore takes the token following a role name
 * to be the actor, without testing what kind of token it is.
 */
typedef struct s_PartBuilder {
	enum BuilderState {
		STATE_EMPTY, STATE_HAS_NAME, STATE_REFLECTION, STATE_COMPLETE
	} state;
	Atom role;
	TypedAtom actor;
	// Keep a pointer to the nested builder, allocated only in STATE_REFLECTION.
	struct s_FormulaBuilder * formulaBuilder;
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
