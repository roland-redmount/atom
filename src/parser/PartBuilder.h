#ifndef PARTBUILDER_H
#define PARTBUILDER_H

#include "parser/Token.h"


struct s_FormulaBuilder;
struct s_TermFormBuilder;


/**
 * Whether the formula parsed by a builder is inside a reflection [ ... ].
 * A quote ^ escapes a variable from a reflection, so a quoted variable ^x
 * can only occur inside a reflection. See also QuoteVariable().
 */
enum FormulaScope {
	FORMULA_TOP_SCOPE = 1,
	FORMULA_REFLECTED_SCOPE = 2,
};


/**
 * A part is a role name followed by an actor. The actor may be written as a
 * reflection [ ... ], which may be either a reflected name or formula.
 * For a reflected formula,  the part builder collects the tokens of
 * the reflection in a nested formula builder while in STATE_REFLECTION, and
 * the formula atom it yields becomes the actor.
 * A reflected name [name] yields an AT_NAME actor.
 *
 * A relation is written [: ... ] and yields an AT_RELATION actor. The part builder
 * collects its tokens in a nested formula builder, as for a reflected formula.
 *
 * A form is written [. ... ] and yields the term form as an AT_ID actor. The part builder
 * collects its tokens in a nested term form builder while in STATE_REFLECTED_FORM.
 *
 * A part builder holds the same alternation as the tokenizer, which reads an actor after
 * a role name; see enum TokenizerState. It therefore takes the token following a role name
 * to be the actor, without testing what kind of token it is. The exceptions are the tokens
 * that begin a reflection, and TOKEN_END_REFLECT, which closes a reflected name [name].
 */
typedef struct s_PartBuilder {
	enum BuilderState {
		STATE_EMPTY, STATE_HAS_NAME, STATE_REFLECTION_START, STATE_REFLECTED_NAME,
		STATE_REFLECTION, STATE_REFLECTED_FORM, STATE_COMPLETE
	} state;
	Atom role;
	TypedAtom actor;
	// whether this part is inside a reflected formula
	enum FormulaScope scope;
	// either AT_FORMULA or AT_RELATION
	byte reflectionType;
	// Keep a pointer to the nested builder, allocated only in STATE_REFLECTION.
	struct s_FormulaBuilder * formulaBuilder;
	// CLAUDE: The nested builder of a reflected form, allocated only in STATE_REFLECTED_FORM
	struct s_TermFormBuilder * termFormBuilder;
} PartBuilder;


void InitializePartBuilder(PartBuilder * builder, enum FormulaScope scope);

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
