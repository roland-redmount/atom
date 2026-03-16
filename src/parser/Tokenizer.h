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
			byte io;
			byte datumType;
		} parameter;
	} data;
};

typedef struct s_Tokenizer Tokenizer;


void TokenizerInit(Tokenizer * tokenizer);
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
 * Convenience function to read a single token from a C string.
 * The string must contain a valid token, or an ASSERT will be triggered.
 */
Token CreateTokenFromCString(char const * cString);


#endif	// TOKENIZER_H
