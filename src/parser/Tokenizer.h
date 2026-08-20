/**
 * A Tokenizer accepts a stream of characters and generates a stream of Tokens.
 * This is designed to support interactive editing, pushing one token at a time
 * while maintaining a state that can always yield a valid token.
 *
 * The tokenizer knows only as much syntax as its mode, which says whether it is reading a
 * role name or an actor; see enum TokenizerMode. Within a mode it will produce any
 * sequence of tokens that mode allows, e.g. foo & & | in role mode, and leaves the rest of
 * the syntax to the builders.
 */

#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "parser/Token.h"
#include "parser/StringBuffer.h"


/**
 * Syntax alternates between a role name and its actor, and the mode says which of the two
 * the tokenizer is reading. This is what lets a bare word be a role name in one place and
 * a variable in the other, so that a variable needs no prefix to mark it.
 *
 * The two sets of tokens are disjoint, and a token of the wrong one is a syntax error.
 */
enum TokenizerMode {
	TOKENIZER_ROLE_MODE = 1,	// TOKEN_NAME, TOKEN_NOT, TOKEN_OR, TOKEN_AND, TOKEN_END_REFLECT
	TOKENIZER_ACTOR_MODE = 2,	// TOKEN_NUMBER, TOKEN_STRING, TOKEN_LETTER, TOKEN_VARIABLE, TOKEN_PARAMETER, TOKEN_BEGIN_REFLECT
};


struct s_Tokenizer {
	bool isValid;				// true if tokenizer state represents a valid token

	bool isFull;				// if false, tokenizer can receive additonal characters (e.g. continue adding to a name)
								// if true, the token is complete and no more characters are valid,
								// e.g. a variable "x" or a terminated string "\"foo\""

	enum TokenizerMode mode;	// which of a role name and an actor is being read
	enum TokenType type;		// initially TOKEN_INVALID
	StringBuffer buffer;
	union {
		struct {
			uint8 number;
			byte io;
			byte atomType;
		} parameter;
	} data;
};

typedef struct s_Tokenizer Tokenizer;


/**
 * Initialize a tokenizer in TOKENIZER_ROLE_MODE, which is where a formula begins.
 */
void TokenizerInit(Tokenizer * tokenizer);

/**
 * Set the mode the next token is read in. This is for a caller reading a token outside the
 * context that would set the mode; a caller reading a whole formula never needs it, since
 * TokenizerReset() follows the syntax.
 */
void TokenizerSetMode(Tokenizer * tokenizer, enum TokenizerMode mode);

/**
 * Push one character, and return true if the character was accepted.
 * A false return means the character was not consumed, for one of two reasons:
 * 1) if the tokenizer is complete after the call, the character ended the current
 *    token and must be pushed again once that token has been handled and the tokenizer
 *    is reset; see TokenizeCString().
 * 2) if the tokenizer is not complete after the call, the character is a syntax error.
 */
bool TokenizerPush(Tokenizer * tokenizer, char c);

bool TokenizerComplete(Tokenizer const * tokenizer);

/**
 * Reset the tokenizer.
 * This must be called when a tokenizer is complete,
 * before additional characters can be tokenized.
 * The mode of the next token follows from the token just completed, so a tokenizer reset
 * on an abandoned token is left in a mode that means nothing; see enum TokenizerMode.
 */
void TokenizerReset(Tokenizer * tokenizer);

/**
 * Return the token represented by a Tokenizer.
 * This can be called when a token is complete.
 * If the token corresponding to a "literal" atom
 * (numbers, strings, parameters ...) then the
 * returned token contains an atom that must be
 * released by the caller.
 */
Token TokenizerGetToken(Tokenizer const * tokenizer);

void TokenizerCleanup(Tokenizer * tokenizer);


/**
 * Read a single token from a C string, in the given mode. The string must contain a valid
 * token of that mode, or an ASSERT will be triggered. Any characters past the first valid
 * token are ignored: for example, the string "foo 123" read in TOKENIZER_ROLE_MODE will
 * only return a token for the name "foo".
 */
Token CreateTokenFromCString(char const * cString, enum TokenizerMode mode);


/**
 * A function receiving the tokens read by TokenizeCString(), returning false
 * if it cannot accept a token.
 */
typedef bool (*TokenHandler)(void * context, Token token);

/**
 * Tokenize a whole C string, passing each token found to given TokenHandler,
 * along with the given context pointer.
 * Every token is released once the handler has processed it.
 * The string must be valid syntax that the handler accepts,
 * or an ASSERT will be triggered.
 */
void TokenizeCString(char const * cString, TokenHandler handler, void * context);


#endif	// TOKENIZER_H
