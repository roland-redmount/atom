

#ifndef PARTBUILDER_H
#define PARTBUILDER_H


#include "parser/Token.h"


typedef struct s_PartBuilder {
	enum BuilderState {STATE_EMPTY, STATE_PARTIAL, STATE_COMPLETE} state;
	Atom role;
	TypedAtom actor;
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
