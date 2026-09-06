#include "kernel/float.h"
#include "kernel/Int.h"
#include "kernel/letter.h"
#include "kernel/Parameter.h"
#include "lang/name.h"
#include "lang/Variable.h"
#include "library/string.h"
#include "parser/Characters.h"
#include "parser/Tokenizer.h"


void TokenizerInit(Tokenizer * tokenizer, enum TokenizerInputMode inputMode)
{
	SetMemory(tokenizer, sizeof(Tokenizer), 0);
	StringBufferInit(&(tokenizer->buffer));
	tokenizer->state = TOKENIZER_ROLE_STATE;
	tokenizer->inputMode = inputMode;
}


/**
 * Set the tokenizer to full, and handle the needsSeparator flag.
 * A variable and a letter need a separator in string input mode,
 * since a name character pushed next would continue the variable or the letter
 * instead of beginning a token of its own; see enum TokenizerInputMode.
 */
static void tokenizerSetFull(Tokenizer * tokenizer)
{
	tokenizer->isFull = true;
	tokenizer->needsSeparator =
		(tokenizer->type == TOKEN_VARIABLE) || (tokenizer->type == TOKEN_LETTER);
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

	tokenizerSetFull(tokenizer);
	return true;
}


/**
 * Mark the current token complete (valid, full) and set its type.
 */
static enum TokenizerResult beginSingleCharacterToken(
	Tokenizer * tokenizer, enum TokenType type)
{
	tokenizer->type = type;
	tokenizer->isValid = true;
	tokenizerSetFull(tokenizer);
	return TOKENIZER_ACCEPTED;
}


/**
 * Process the character c in TOKENIZER_ROLE_STATE to begin a new token
 */
static enum TokenizerResult roleStateBeginToken(Tokenizer * tokenizer, char c)
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
			return TOKENIZER_ACCEPTED;
		}
		return TOKENIZER_REJECTED;
	}
}


/**
 * Process the character c in TOKENIZER_ACTOR_STATE to begin a new token
 */
static enum TokenizerResult actorStateBeginToken(Tokenizer * tokenizer, char c)
{
	switch(c) {
	case '"':
		tokenizer->type = TOKEN_STRING;
		tokenizer->isValid = false;
		return TOKENIZER_ACCEPTED;

	case '\'':
		// a letter is written 'A, and needs no closing quote since it is one character
		tokenizer->type = TOKEN_LETTER;
		tokenizer->isValid = false;
		return TOKENIZER_ACCEPTED;

	case '_':
		// the anonymous variable, which is complete unless a name character follows
		tokenizer->type = TOKEN_VARIABLE;
		tokenizer->data.variable.isQuoted = false;
		tokenizer->isValid = true;
		tokenizerSetFull(tokenizer);
		return TOKENIZER_ACCEPTED;

	case '^':
		// being a quoted variable
		tokenizer->type = TOKEN_VARIABLE;
		tokenizer->data.variable.isQuoted = true;
		tokenizer->isValid = false;
		return TOKENIZER_ACCEPTED;

	case '@':
		tokenizer->type = TOKEN_PARAMETER;
		tokenizer->isValid = true;
		return TOKENIZER_ACCEPTED;

	case '*':
		return beginSingleCharacterToken(tokenizer, TOKEN_GENERATOR);

	case '[':
		return beginSingleCharacterToken(tokenizer, TOKEN_BEGIN_REFLECT);

	default:
		if(IsDigitChar(c)) {
			tokenizer->type = TOKEN_NUMBER;
			StringBufferPush(&(tokenizer->buffer), c);
			tokenizer->isValid = true;
			return TOKENIZER_ACCEPTED;
		}
		// A variable is named by a single letter.
		if(IsAlpha(c)) {
			tokenizer->type = TOKEN_VARIABLE;
			tokenizer->data.variable.isQuoted = false;
			StringBufferPush(&(tokenizer->buffer), c);
			tokenizer->isValid = true;
			tokenizerSetFull(tokenizer);
			return TOKENIZER_ACCEPTED;
		}
		return TOKENIZER_REJECTED;
	}
}


enum TokenizerResult TokenizerPush(Tokenizer * tokenizer, char c)
{
	if(tokenizer->isFull) {
		if(IsWhiteSpace(c) || (c == 0)) {
			// trailing whitespace or termination char
			tokenizer->needsSeparator = false;
			return TOKENIZER_ACCEPTED;
		}
		// A name character is rejected in string input mode if a separator is needed.
		if((tokenizer->inputMode == TOKENIZER_STRING_INPUT)	&& tokenizer->needsSeparator && IsNameChar(c))
			return TOKENIZER_REJECTED;
		// Any other character is valid syntax, but caller must pop the completed token first
		return TOKENIZER_ENDED;
	}

	switch(tokenizer->type) {
	case TOKEN_NONE:
		if(IsWhiteSpace(c) || (c == 0)) {
			// leading whitespace separates the token just popped from the next
			tokenizer->needsSeparator = false;
			return TOKENIZER_ACCEPTED;
		}
		// A name character is rejected in string input mode if a separator is needed.
		if((tokenizer->inputMode == TOKENIZER_STRING_INPUT)	&& tokenizer->needsSeparator && IsNameChar(c))
			return TOKENIZER_REJECTED;
		// the state decides which tokens may begin here
		if(tokenizer->state == TOKENIZER_ACTOR_STATE)
			return actorStateBeginToken(tokenizer, c);
		else
			return roleStateBeginToken(tokenizer, c);

	case TOKEN_NAME:
		if(IsNameChar(c)) {
			StringBufferPush(&(tokenizer->buffer), c);
			return TOKENIZER_ACCEPTED;
		}
		if(IsWhiteSpace(c) || (c == 0)) {
			// whitespace terminates name
			tokenizerSetFull(tokenizer);
			return TOKENIZER_ACCEPTED;
		}
		if(IsSeparatorChar(c)) {
			// a separator terminates the name
			tokenizerSetFull(tokenizer);
			return TOKENIZER_ENDED;
		}
		return TOKENIZER_REJECTED;

	case TOKEN_NUMBER:
		// TODO: handle minus sign
		// NOTE: do the number conversion here?
		if(IsDigitChar(c)) {
			StringBufferPush(&(tokenizer->buffer), c);
			return TOKENIZER_ACCEPTED;
		}
		if(c == '.') {
			// decimal point may occur only once
			if(StringContainsChar(
					tokenizer->buffer.buffer, tokenizer->buffer.stringLength, '.'))
				return TOKENIZER_REJECTED;
			else {
				StringBufferPush(&(tokenizer->buffer), c);
				return TOKENIZER_ACCEPTED;
			}
		}
		if(IsWhiteSpace(c) || (c == 0)) {
			// whitespace terminates number
			tokenizerSetFull(tokenizer);
			return TOKENIZER_ACCEPTED;
		}
		if(IsSeparatorChar(c)) {
			// a separator terminates the number and begins a token of its own
			tokenizerSetFull(tokenizer);
			return TOKENIZER_ENDED;
		}
		return TOKENIZER_REJECTED;

	case TOKEN_STRING:
		// string token is incomplete until closing "
		if(c == '"') {
			tokenizer->isValid = true;
			tokenizerSetFull(tokenizer);
			return TOKENIZER_ACCEPTED;
		}
		// valid string characters
		if(IsPrintableChar(c)) {
			StringBufferPush(&(tokenizer->buffer), c);
			return TOKENIZER_ACCEPTED;
		}
		return TOKENIZER_REJECTED;

	case TOKEN_LETTER:
		// CLAUDE: the letter itself, which the opening quote is still waiting for. A letter
		// is one character, so the token is full once the letter has been read.
		if(!IsAlpha(c))
			return TOKENIZER_REJECTED;
		StringBufferPush(&(tokenizer->buffer), c);
		tokenizer->isValid = true;
		tokenizerSetFull(tokenizer);
		return TOKENIZER_ACCEPTED;

	case TOKEN_VARIABLE:
		// CLAUDE: A variable named by a letter alone is full at that letter, so the quote
		// character (^) waiting for its variable name is the only state left here.
		ASSERT(tokenizer->data.variable.isQuoted)
		if(!IsAlpha(c))
			return TOKENIZER_REJECTED;
		StringBufferPush(&(tokenizer->buffer), c);
		tokenizer->isValid = true;
		tokenizerSetFull(tokenizer);
		return TOKENIZER_ACCEPTED;

	case TOKEN_PARAMETER:
		if(!tokenizer->data.parameter.number) {
			// parse parameter number (positive integer)
			if(IsDigitChar(c)) {
				StringBufferPush(&(tokenizer->buffer), c);
				return TOKENIZER_ACCEPTED;
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
					return TOKENIZER_REJECTED;
				// continue with current character
			}
		}
		if(!tokenizer->data.parameter.io) {
			// parse io type
			if(c == '<') {
				tokenizer->data.parameter.io = PARAMETER_IN;
				return TOKENIZER_ACCEPTED;
			}
			else if(c == '>') {
				tokenizer->data.parameter.io = PARAMETER_OUT;
				return TOKENIZER_ACCEPTED;
			}
			else
				return TOKENIZER_REJECTED;
		}
		// parse atom type name
		// TODO: here we must ensure that the string
		// is always a prefix of valid type name.
		if(IsNameChar(c)) {
			StringBufferPush(&(tokenizer->buffer), c);
			return TOKENIZER_ACCEPTED;
		}
		if(IsWhiteSpace(c) || (c == 0)) {
			// whitespace completes parameter
			return finishParameter(tokenizer) ? TOKENIZER_ACCEPTED : TOKENIZER_REJECTED;
		}
		if(IsSeparatorChar(c)) {
			// a separator completes the parameter but begins a token of its own,
			// so the character is left for the caller to push again
			return finishParameter(tokenizer) ? TOKENIZER_ENDED : TOKENIZER_REJECTED;
		}
		return TOKENIZER_REJECTED;

	default:
		// should never occur
		ASSERT(false);
		return TOKENIZER_REJECTED;
	}
}


bool TokenizerIsFull(Tokenizer const * tokenizer)
{
	return tokenizer->isFull;
}


/**
 * Set the tokenizer state for the next token. A role name is followed by an actor,
 * and everything else by a role name.
 */
static enum TokenizerState nextState(enum TokenType type)
{
	return (type == TOKEN_NAME) ? TOKENIZER_ACTOR_STATE : TOKENIZER_ROLE_STATE;
}


/**
 * Clear the tokenizer and set the state, leaving the tokenizer ready to read a new token.
 * The needsSeparator flag is not altered, as it affects the tokenizer behavior on
 * reading the next token.
 */
static void clearToken(Tokenizer * tokenizer, enum TokenizerState state)
{
	tokenizer->state = state;
	tokenizer->isValid = false;
	tokenizer->isFull = false;
	tokenizer->type = TOKEN_NONE;
	tokenizer->data.value = 0;

	StringBufferReset(&(tokenizer->buffer));
}


void TokenizerReset(Tokenizer * tokenizer)
{
	// CLAUDE: a reset follows the syntax, which only a completed token says anything about
	ASSERT(tokenizer->isFull)
	clearToken(tokenizer, nextState(tokenizer->type));
}


void TokenizerRestart(Tokenizer * tokenizer, enum TokenizerState state)
{
	// CLAUDE: nothing read before this point bears on the token read next
	tokenizer->needsSeparator = false;
	clearToken(tokenizer, state);
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

	case TOKEN_LETTER:
		token.typedAtom = CreateTypedAtom(AT_LETTER, GetAlphabetLetter(string[0]));
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

	case TOKEN_GENERATOR:
		token.typedAtom = generatorAtom;
		break;

	default:
		// tokens that do not represent an atom
		token.typedAtom = invalidAtom;
	}
	return token;
}


Token CreateTokenFromCString(char const * cString, enum TokenizerState state)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);
	TokenizerRestart(&tokenizer, state);
	char const * p = cString;
	while(*p) {
		ASSERT(TokenizerPush(&tokenizer, *p++) == TOKENIZER_ACCEPTED);
		if(tokenizer.isFull)
			break;	// token complete before string ends
	}
	// A token running to the end of the string is completed by the terminator, as
	// whitespace completes a token followed by another. A token ending in a character
	// of its own, such as a quoted string, is already complete.
	if(!tokenizer.isFull)
		ASSERT(TokenizerPush(&tokenizer, 0) == TOKENIZER_ACCEPTED);
	ASSERT(TokenizerIsFull(&tokenizer));
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
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);
	// NOTE: including the 0 terminator, which completes the last token
	for(index32 i = 0; i <= length; i++) {
		enum TokenizerResult result = TokenizerPush(&tokenizer, cString[i]);
		ASSERT(result != TOKENIZER_REJECTED)
		if(result == TOKENIZER_ENDED) {
			// the character ended the token before it without being part of it,
			// and has to be pushed again once that token has been taken
			handleToken(&tokenizer, handler, context);
			ASSERT(TokenizerPush(&tokenizer, cString[i]) == TOKENIZER_ACCEPTED)
		}
		if(TokenizerIsFull(&tokenizer))
			handleToken(&tokenizer, handler, context);
	}
	TokenizerCleanup(&tokenizer);
}
