/**
 * A Tokenizer accepts a stream of characters and generates a stream of Tokens.
 * It can operate in two input modes, an "interactive" mode to support interactive editing,
 * and a "string" mode for parsing a given syntax string. The tokenizer is used by
 * "builder" methods such as FormulaBuilder to parse syntax. Typical usage:
 * 
 * while(...) {
 *   TokenizerPush(tokenizer, c);
 *   if(TokenizerIsFull(tokenizer)) {
 *     TokenizerGetToken(tokenizer)
 *     // do something with token ...
 * 	   TokenizerReset()
 *   }
 * }
 * 
 * The tokenizer alternates between two states, determining whether it is reading
 * a role name or an actor; see enum TokenizerState. This is the only context-dependence
 * of the tokenizer. Besides this, the tokenizer has no knowledge of syntax, and 
 * will produce any sequence of tokens.
 */

#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "parser/Token.h"
#include "parser/StringBuffer.h"


/**
 * The tokenizer alternates between two states, reading either a role or an actor.
 * This affords some context-sensitiviy, so that 'x' can indicate a name in the role state,
 * but a variable in the actor state. Each token is can occur only in one of the two states,
 * as indicated below.
 */
enum TokenizerState {
	// When reading a role, these tokens are allowed:
	// TOKEN_NAME, TOKEN_NOT, TOKEN_OR, TOKEN_AND, TOKEN_END_REFLECT
	TOKENIZER_ROLE_STATE = 1,
	// When reading an actor, these tokens are allowed:
	// TOKEN_NUMBER, TOKEN_STRING, TOKEN_LETTER, TOKEN_VARIABLE, TOKEN_PARAMETER, TOKEN_BEGIN_REFLECT,
	// TOKEN_GENERATOR
	TOKENIZER_ACTOR_STATE = 2,
};


/**
 * The tokenizer's input mode determines whether one token must be separated
 * from the next. This concerns a variable and a letter, since each ends in a name character
 * that a further name character could have been part of.
 *
 * Interactive input mode takes such a character as the start of the next token, so that the input
 * sequence "foo xy 3" parses into (foo x y 3), with the character 'y' beginning a new name,
 * String input mode on the other hand rejects "foo xy 3" as invalid syntax, since a separator character
 * is required after 'x'. See ParseFormula().
 */
enum TokenizerInputMode {
	TOKENIZER_INTERACTIVE_INPUT = 1,
	TOKENIZER_STRING_INPUT = 2,
};


/**
 * Possible return values for TokenizerPush(), describing what the tokeninzer
 * did with the pushed character.
 */
enum TokenizerResult {
	// The character was accepted as part of the token being read
	TOKENIZER_ACCEPTED = 1,
	// The character is legal, but completed the previous token and was not used.
	// The caller must push the character again after popping the current token.
	TOKENIZER_ENDED = 2,
	// The character is illegal at this position.
	TOKENIZER_REJECTED = 3,
};


struct s_Tokenizer {
	bool isValid;				// true if tokenizer state represents a valid token

	// The tokenizer is "full" when no more characters can be added to the current token:
	// for example, a variable "x" or a  terminated string "\"foo\"".
	// A name, a number and a parameter are not full until a terminator arrives.
	// A full tokenizer is always valid.
	bool isFull;

	// Whether a name character pushed next would continue the completed token
	// rather than begin the token after it. This occurs for TOKEN_VARIABLE and TOKEN_LETTER.
	// Whitespace clears this; see enum TokenizerInputMode.
	bool needsSeparator;

	enum TokenizerInputMode inputMode;
	enum TokenizerState state;
	enum TokenType type;		// type of the token currently being read; initially 0 (TOKEN_NONE)
	StringBuffer buffer;
	union {
		struct {
			uint8 number;
			byte io;
			byte atomType;
		} parameter;
		struct {
			bool isQuoted;
		} variable;
		data64 value;	// used only to clear the union
	} data;
};

typedef struct s_Tokenizer Tokenizer;


/**
 * Initialize a tokenizer in TOKENIZER_ROLE_STATE, which is where a formula begins.
 * inputMode says where the characters come from; see enum TokenizerInputMode.
 */
void TokenizerInit(Tokenizer * tokenizer, enum TokenizerInputMode inputMode);

/**
 * Restart the tokenizer to begin reading a token in the given state, discarding whatever
 * the tokenizer has read previously.
 */
void TokenizerRestart(Tokenizer * tokenizer, enum TokenizerState state);

/**
 * Push one character, adding to the token being read.
 * For the return value; see enum TokenizerResult.
 */
enum TokenizerResult TokenizerPush(Tokenizer * tokenizer, char c);

/**
 * The tokenizer is "full" when no more characters can be added to the current token.
 * TokenizerGetToken() can then be used to obtain the token. TokenizerReset()
 * must be called before additional tokens can be accepted.
 */
bool TokenizerIsFull(Tokenizer const * tokenizer);

/**
 * Reset the tokenizer. This must be called when TokenizerIsFull() returns true,
 * before additional characters can be pushed. It is an error to call TokenizerReset()
 * if the tokenizer is not full.
 * CLAUDE: The token just read decides the state the next one is read in, so a tokenizer
 * reading a whole formula follows the syntax on its own; see enum TokenizerState. Use
 * TokenizerRestart() to begin somewhere the syntax has not led the tokenizer.
 */
void TokenizerReset(Tokenizer * tokenizer);

/**
 * Return the current token. This can be called when the tokenized is valid.
 * The returned Token contains an atom that must be released by the caller.
 */
Token TokenizerGetToken(Tokenizer const * tokenizer);

/**
 * Deallocate tokenizer storage.
 */
void TokenizerFree(Tokenizer * tokenizer);

/**
 * Read a single token from a C string, in the given tokenizer state. The string must a
 * token valid in that state, or an ASSERT will be triggered. Any characters follwing the
 * first valid token are ignored: for example, the string "foo 123" read in
 * TOKENIZER_ROLE_STATE will only return a token for the name "foo".
 */
Token CreateTokenFromCString(char const * cString, enum TokenizerState state);

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
