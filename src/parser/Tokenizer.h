/**
 * A Tokenizer accepts a stream of characters and generates a stream of Tokens.
 * This is designed to support interactive editing, pushing one token at a time
 * while maintaining a state that can always yield a valid token.
 */

#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "parser/Token.h"
#include "parser/StringBuffer.h"


struct s_Tokenizer {
	bool isValid;				// true if tokenizer state represents a valid token

	bool isFull;				// if false, tokenizer can receive additonal characters (e.g. continue adding to a name)
								// if true, the token is complete and no more characters are valid,
								// e.g. a variable "_x" or a terminated string "\"foo\""

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


void TokenizerInit(Tokenizer * tokenizer);

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
 * Read a single token from a C string. The string must contain a valid token,
 * or an ASSERT will be triggered. Any characters past the first valid token
 * are ignored: for example, the string "foo 123" will only return a token
 * for the name "foo".
 */
Token CreateTokenFromCString(char const * cString);


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
