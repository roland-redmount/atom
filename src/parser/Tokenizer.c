/**
 * The tokenizer converts a stream of characters into stream of tokens,
 * which are then parsed by the builder methods. Typical usage:
 * 
 * while(...) {
 *   TokenizerPush(tokenizer, c);
 *   if(TokenizerComplete(tokenizer)) {
 *     // new token becomes available when whitespace is pushed
 *     TokenizerGetToken(tokenizer)
 *     // do something with token ...
 * 	   TokenizerReset()
 *   }
 * }
 * 
 * The tokenizer understands one thing about syntax: whether it is reading a role name or
 * an actor, which its mode says; see enum TokenizerMode. Within a mode it will produce any
 * sequence of tokens that mode allows, e.g. foo & & | ! ! in role mode.
 * 
 * We could generate a tokenizer with a standard lexical analysis tool
 * such as lex, but it may be difficult to customize for interactive settings.
 * E.g. for input methods that allow editing (e.g. backspace) we need a
 * tokenizer with a "take back" functionality.
 */

#include "kernel/float.h"
#include "kernel/Int.h"
#include "kernel/Parameter.h"
#include "lang/Variable.h"
#include "kernel/string.h"
#include "lang/name.h"
#include "parser/Characters.h"
#include "parser/Tokenizer.h"


void TokenizerInit(Tokenizer * tokenizer)
{
	SetMemory(tokenizer, sizeof(Tokenizer), 0);
	StringBufferInit(&(tokenizer->buffer));
	tokenizer->mode = TOKENIZER_ROLE_MODE;
}


void TokenizerSetMode(Tokenizer * tokenizer, enum TokenizerMode mode)
{
	tokenizer->mode = mode;
}


/**
 * Determine the atom type of a parameter from the type name accumulated in the
 * buffer, and mark the token complete. A parameter with no type name is untyped.
 * Returns false, leaving the token incomplete, when the buffer holds no known type
 * name. That is a syntax error rather than a programmer error, since the type name
 * is typed by a user; see ParseFormula().
 */
static bool finishParameter(Tokenizer * tokenizer)
{
	if(tokenizer->buffer.stringLength > 0) {
		tokenizer->data.parameter.atomType = AtomTypeFromString(
			tokenizer->buffer.buffer,
			tokenizer->buffer.stringLength
		);
		if(!tokenizer->data.parameter.atomType)
			return false;
	}
	else
		tokenizer->data.parameter.atomType = 0;

	tokenizer->isFull = true;
	return true;
}


/**
 * Mark the token complete, given its type and that a single character decided it.
 */
static bool beginSingleCharacterToken(Tokenizer * tokenizer, enum TokenType type)
{
	tokenizer->type = type;
	tokenizer->isValid = true;
	tokenizer->isFull = true;
	return true;
}


/**
 * Begin the token a character starts in TOKENIZER_ROLE_MODE, which is a role name or one
 * of the operators standing between one part of a formula and the next.
 */
static bool beginRoleToken(Tokenizer * tokenizer, char c)
{
	switch(c) {
	case '&':
		return beginSingleCharacterToken(tokenizer, TOKEN_AND);

	case '|':
		return beginSingleCharacterToken(tokenizer, TOKEN_OR);

	case '!':
		return beginSingleCharacterToken(tokenizer, TOKEN_NOT);

	case ']':
		// closing a reflection, whose formula ended with an actor
		return beginSingleCharacterToken(tokenizer, TOKEN_END_REFLECT);

	default:
		if(IsNameInitialChar(c)) {
			tokenizer->type = TOKEN_NAME;
			StringBufferPush(&(tokenizer->buffer), c);
			tokenizer->isValid = true;
			return true;
		}
		if(IsWhiteSpace(c) || (c == 0)) {
			// leading whitespace does nothing
			return true;
		}
		return false;
	}
}


/**
 * Begin the token a character starts in TOKENIZER_ACTOR_MODE, which is an actor: a number,
 * a string, a variable, a parameter, or the reflection holding a formula as an actor.
 */
static bool beginActorToken(Tokenizer * tokenizer, char c)
{
	switch(c) {
	case '"':
		tokenizer->type = TOKEN_STRING;
		tokenizer->isValid = false;
		return true;

	case '_':
		// the anonymous variable, which is complete unless a name character follows
		tokenizer->type = TOKEN_VARIABLE;
		tokenizer->isValid = true;
		return true;

	case '@':
		tokenizer->type = TOKEN_PARAMETER;
		tokenizer->isValid = true;
		return true;

	case '[':
		return beginSingleCharacterToken(tokenizer, TOKEN_BEGIN_REFLECT);

	default:
		if(IsDigitChar(c)) {
			tokenizer->type = TOKEN_NUMBER;
			StringBufferPush(&(tokenizer->buffer), c);
			tokenizer->isValid = true;
			return true;
		}
		// A variable is named by a single letter, so only a letter begins one. A role
		// name may hold characters that are not letters, such as the + of (+ 2 + 3 = 5),
		// and none of those names an actor; see CreateVariable().
		if(IsAlpha(c)) {
			tokenizer->type = TOKEN_VARIABLE;
			StringBufferPush(&(tokenizer->buffer), c);
			tokenizer->isValid = true;
			return true;
		}
		if(IsWhiteSpace(c) || (c == 0)) {
			// leading whitespace does nothing
			return true;
		}
		return false;
	}
}


// return true if character was accepted
bool TokenizerPush(Tokenizer * tokenizer, char c)
{
	if(tokenizer->isFull) {
		if(IsWhiteSpace(c) || (c == 0)) {
			// trailing whitespace or termination char
			return true;
		}
		// caller must pop the completed token first
		return false;
	}

	switch(tokenizer->type) {
	case TOKEN_INVALID:
		// the mode decides which tokens may begin here
		if(tokenizer->mode == TOKENIZER_ACTOR_MODE)
			return beginActorToken(tokenizer, c);
		else
			return beginRoleToken(tokenizer, c);

	case TOKEN_NAME:
		if(IsNameChar(c)) {
			StringBufferPush(&(tokenizer->buffer), c);
			return true;
		}
		if(IsWhiteSpace(c) || (c == 0)) {
			// whitespace terminates name
			tokenizer->isFull = true;
			return true;
		}
		if(IsSeparatorChar(c)) {
			// a separator terminates the name and begins a token of its own
			tokenizer->isFull = true;
			return false;
		}
		return false;

	case TOKEN_NUMBER:
		// TODO: handle minus sign
		// NOTE: do the number conversion here?
		if(IsDigitChar(c)) {
			StringBufferPush(&(tokenizer->buffer), c);
			return true;
		}
		if(c == '.') {
			// decimal point may occur only once
			if(StringContainsChar(
					tokenizer->buffer.buffer, tokenizer->buffer.stringLength, '.'))
				return false;
			else {
				StringBufferPush(&(tokenizer->buffer), c);
				return true;
			}
		}
		if(IsWhiteSpace(c) || (c == 0)) {
			// whitespace terminates number
			tokenizer->isFull = true;
			return true;
		}
		if(IsSeparatorChar(c)) {
			// a separator terminates the number and begins a token of its own
			tokenizer->isFull = true;
			return false;
		}
		return false;

	case TOKEN_STRING:
		// string token is incomplete until closing "
		if(c == '"') {
			tokenizer->isValid = true;
			tokenizer->isFull = true;
			return true;
		}
		// valid string characters
		if(IsPrintableChar(c)) {
			StringBufferPush(&(tokenizer->buffer), c);
			return true;
		}
		return false;

	case TOKEN_VARIABLE:
		// The letter naming the variable, or the '_' naming none, has been read already.
		// A further name character is an error rather than the start of the next token,
		// so that a word too long to be a variable is reported where it goes wrong.
		if(IsNameChar(c))
			return false;
		if(IsWhiteSpace(c) || (c == 0)) {
			tokenizer->isFull = true;
			return true;
		}
		if(IsSeparatorChar(c)) {
			// a separator ends the variable and begins a token of its own
			tokenizer->isFull = true;
			return false;
		}
		return false;

	case TOKEN_PARAMETER:
		if(!tokenizer->data.parameter.number) {
			// parse parameter number (positive integer)
			if(IsDigitChar(c)) {
				StringBufferPush(&(tokenizer->buffer), c);
				return true;
			}
			else {
				tokenizer->data.parameter.number = StringToInt64(
					tokenizer->buffer.buffer,
					tokenizer->buffer.stringLength
				);
				StringBufferReset(&tokenizer->buffer);
				// A parameter number is 1-based, so zero means the number is absent.
				// Rejecting zero also rejects an empty number, which yields zero.
				if(!tokenizer->data.parameter.number)
					return false;
				// continue with current character
			}
		}
		if(!tokenizer->data.parameter.io) {
			// parse io type
			if(c == '<') {
				tokenizer->data.parameter.io = PARAMETER_IN;
				return true;
			}
			else if(c == '>') {
				tokenizer->data.parameter.io = PARAMETER_OUT;
				return true;
			}
			else
				return false;
		}
		// parse atom type name
		// TODO: here we must ensure that the string
		// is always a prefix of valid type name.
		if(IsNameChar(c)) {
			StringBufferPush(&(tokenizer->buffer), c);
			return true;
		}
		if(IsWhiteSpace(c) || (c == 0)) {
			// whitespace completes parameter
			return finishParameter(tokenizer);
		}
		if(IsSeparatorChar(c)) {
			// a separator completes the parameter but begins a token of its own,
			// so reject the token (to be pushed again)
			finishParameter(tokenizer);
			return false;
		}
		return false;

	default:
		// should never occur
		ASSERT(false);
		return false;	
	}
}


bool TokenizerComplete(Tokenizer const * tokenizer)
{
	return tokenizer->isFull;
}


/**
 * The mode the token after the given one is read in. A role name is followed by its actor,
 * and everything else by a role name: an actor completes a part, and each of the operators
 * stands before one. A reflection is an actor, and both of its brackets are followed by a
 * role name, the first by the name beginning the reflected formula and the second by the
 * name continuing the predicate the reflection is an actor of.
 */
static enum TokenizerMode nextMode(enum TokenType type)
{
	return (type == TOKEN_NAME) ? TOKENIZER_ACTOR_MODE : TOKENIZER_ROLE_MODE;
}


void TokenizerReset(Tokenizer * tokenizer)
{
	StringBuffer buffer = tokenizer->buffer;
	enum TokenizerMode mode = nextMode(tokenizer->type);
	SetMemory(tokenizer, sizeof(Tokenizer), 0);
	tokenizer->buffer = buffer;
	tokenizer->mode = mode;
	StringBufferReset(&(tokenizer->buffer));
}


void TokenizerCleanup(Tokenizer * tokenizer)
{
	StringBufferCleanup(&(tokenizer->buffer));
}


static TypedAtom parseFloat(char const * syntax, size32 length)
{
	return CreateTypedAtom(AT_FLOAT, (Atom) {._float = StringToFloat64(syntax, length)});
}


static TypedAtom parseInteger(char const * syntax, size32 length)
{
	return CreateTypedAtom(AT_INT, (Atom) {._int = StringToInt64(syntax, length)});
}


Token TokenizerGetToken(Tokenizer const * tokenizer)
{
	ASSERT(tokenizer->isValid)

	Token token;
	token.type = tokenizer->type;
	char const * string = tokenizer->buffer.buffer;
	size32 stringLength = tokenizer->buffer.stringLength;

	switch(tokenizer->type) {
	case TOKEN_STRING:
		// strings entered in formulas are always immutable
		token.typedAtom = CreateTypedAtom(AT_ID, CreateString(string, stringLength));
		break;

	case TOKEN_NUMBER:
		if(StringContainsChar(string, stringLength, '.'))
			token.typedAtom = parseFloat(string, stringLength);
		else
			token.typedAtom = parseInteger(string, stringLength);
		break;

	case TOKEN_VARIABLE:
		// NOTE: variable names must now be a single char
		if(stringLength == 0)
			token.typedAtom = anonymousVariable;
		else
			token.typedAtom = CreateTypedAtom(AT_VARIABLE, CreateVariable(string[0]));
		break;

	case TOKEN_PARAMETER:
		token.typedAtom = CreateTypedAtom(
			AT_PARAMETER,
			// TODO: this could probably be simplified
			(Atom) {
				.parameter = {
					.number = tokenizer->data.parameter.number,
					.io = tokenizer->data.parameter.io,
					.atomType = tokenizer->data.parameter.atomType
				}
			}
		);
		break;
				
	case TOKEN_NAME:
		token.typedAtom = CreateTypedAtom(AT_NAME, CreateName(string, stringLength));
		break;

	default:
		// tokens that do not represent an atom
		token.typedAtom = invalidAtom;
	}
	return token;
}


Token CreateTokenFromCString(char const * cString, enum TokenizerMode mode)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);
	TokenizerSetMode(&tokenizer, mode);
	char const * p = cString;
	while(*p) {
		ASSERT(TokenizerPush(&tokenizer, *p++));
		if(tokenizer.isFull)
			break;	// token complete before string ends
	}
	// A token running to the end of the string is completed by the terminator, as
	// whitespace completes a token followed by another. A token ending in a character
	// of its own, such as a quoted string, is already complete.
	if(!tokenizer.isFull)
		ASSERT(TokenizerPush(&tokenizer, 0));
	ASSERT(TokenizerComplete(&tokenizer));
	Token token = TokenizerGetToken(&tokenizer);
	TokenizerCleanup(&tokenizer);
	return token;
}

/**
 * Send the current token to the handler and reset the tokenizer.
 */
static void handleToken(Tokenizer * tokenizer, TokenHandler handler, void * context)
{
	Token token = TokenizerGetToken(tokenizer);
	ASSERT(handler(context, token))
	ReleaseToken(token);
	TokenizerReset(tokenizer);
}


void TokenizeCString(char const * cString, TokenHandler handler, void * context)
{
	size32 length = CStringLength(cString);
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);
	// NOTE: including the 0 terminator, which completes the last token
	for(index32 i = 0; i <= length; i++) {
		if(!TokenizerPush(&tokenizer, cString[i])) {
			// the character ended the token before it without being part of it,
			// and has to be pushed again once that token has been taken
			ASSERT(TokenizerComplete(&tokenizer))
			handleToken(&tokenizer, handler, context);
			ASSERT(TokenizerPush(&tokenizer, cString[i]))
		}
		if(TokenizerComplete(&tokenizer))
			handleToken(&tokenizer, handler, context);
	}
	TokenizerCleanup(&tokenizer);
}
