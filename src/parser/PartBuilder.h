

#ifndef PARTBUILDER_H
#define PARTBUILDER_H


#include "parser/Token.h"


typedef struct s_PartBuilder {
	enum BuilderState {STATE_EMPTY, STATE_PARTIAL, STATE_COMPLETE} state;
	Atom role;
	Atom actor;
} PartBuilder;


void InitializePartBuilder(PartBuilder * builder);

bool PartBuilderPush(PartBuilder * builder, Token token);

bool PartBuilderIsEmpty(PartBuilder const * builder);

bool PartBuilderComplete(PartBuilder const * builder);

Atom PartBuilderGetRole(PartBuilder const * builder);
Atom PartBuilderGetActor(PartBuilder const * builder);

void PartBuilderReset(PartBuilder * builder);


#endif	// PARTBUILDER_H
