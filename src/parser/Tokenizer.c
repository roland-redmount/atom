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
 * The tokenizer does not understand syntax and will produce any
 * sequence of valid tokens, e.g. foo & & | 42 345.12 "foo" ! !  ...
 * 
 * We could generate a tokenizer with a standard lexical analysis tool
 * such as lex, but it may be difficult to customize for interactive settings.
 * E.g. for input methods that allow editing (e.g. backspace) we need a
 * tokenizer with a "take back" functionality.
 */

#include "datumtypes/FloatIEEE754.h"
#include "datumtypes/Int.h"
#include "datumtypes/Parameter.h"
#include "datumtypes/register.h"
#include "datumtypes/Variable.h"
#include "kernel/string.h"
#include "lang/name.h"
#include "parser/Characters.h"
#include "parser/Tokenizer.h"


void TokenizerInit(Tokenizer * tokenizer)
{
	SetMemory(tokenizer, sizeof(Tokenizer), 0);
	StringBufferInit(&(tokenizer->buffer));
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
		// determine the token type from the first character
		if(c == '&') {
			tokenizer->type = TOKEN_AND;
			tokenizer->isValid = true;
			tokenizer->isFull = true;
			return true;
		}
		if(c == '|') {
			tokenizer->type = TOKEN_OR;
			tokenizer->isValid = true;
			tokenizer->isFull = true;
			return true;
		}
		if(c == '!') {
			tokenizer->type = TOKEN_NOT;
			tokenizer->isValid = true;
			tokenizer->isFull = true;
			return true;
		}
		if(c == '"') {
			tokenizer->type = TOKEN_STRING;
			tokenizer->isValid = false;
			return true;
		}
		if(c == '_') {
			tokenizer->type = TOKEN_VARIABLE;
			tokenizer->isValid = false;
			return true;
		}
		if(c == '@') {
			tokenizer->type = TOKEN_IN_PARAMETER;
			tokenizer->isValid = false;
			return true;
		}
		if(c == '$') {
			tokenizer->type = TOKEN_OUT_PARAMETER;
			tokenizer->isValid = false;
			return true;
		}
		if(c == '#') {
			tokenizer->type = TOKEN_REGISTER;
			tokenizer->isValid = false;
			return true;
		}
		if(IsDigitChar(c)) {
			tokenizer->type = TOKEN_NUMBER;
			StringBufferPush(&(tokenizer->buffer), c);
			tokenizer->isValid = true;
			return true;
		}
		if(IsNameInitialChar(c)) {
			tokenizer->type = TOKEN_NAME;
			StringBufferPush(&(tokenizer->buffer), c);
			tokenizer->isValid = true;
			return true;
		}
		if(IsWhiteSpace(c) || (c == 0)) {
			// leading whitespace does nothibng
			return true;
		}
		return false;

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
		// accept a single name char
		if(IsNameChar(c)) {
			StringBufferPush(&(tokenizer->buffer), c);
			tokenizer->isValid = true;
			tokenizer->isFull = true;
			return true;
		}
		if(IsWhiteSpace(c) || (c == 0)) {
			// lone '_', anonymous variable, string is empty
			tokenizer->isValid = true;
			tokenizer->isFull = true;
			return true;
		}
		return false;

	case TOKEN_IN_PARAMETER:
	case TOKEN_OUT_PARAMETER:
		if(tokenizer->data.parameter.index == 0) {
			// parse integer index
			if(IsDigitChar(c)) {
				StringBufferPush(&(tokenizer->buffer), c);
				tokenizer->isValid = true;
				return true;
			}
			else if(IsWhiteSpace(c) || (c == 0) || (c == ':')) {
				// terminate parameter index
				tokenizer->data.parameter.index = StringToInt64(
					tokenizer->buffer.buffer, tokenizer->buffer.stringLength);
				StringBufferReset(&(tokenizer->buffer));

				// whitespace terminates parameter, no type specifier
				if(c == ':') {
					// expect type specifier next
					return true;
				}
				else {
					tokenizer->isFull = true;
					return true;
				}
			}
			else
				return false;
		}
		else {
			// collect type name
			// TODO: here we must ensure that the string
			// is always a prefix of valid type name.
			if(IsNameChar(c)) {
				StringBufferPush(&(tokenizer->buffer), c);
				return true;
			}
			if(IsWhiteSpace(c) || (c == 0)) {
				// whitespace terminates parameter
				if(tokenizer->buffer.stringLength > 0) {
					tokenizer->data.parameter.type = DatumTypeIdFromString(
						tokenizer->buffer.buffer,
						tokenizer->buffer.stringLength
					);
					ASSERT(tokenizer->data.parameter.type);
				}
				else
					tokenizer->data.parameter.type = 0;	// untyped parameter

				tokenizer->isFull = true;
				return true;
			}
			else
				return false;
		}
	
	case TOKEN_REGISTER:
		// parse register index
		if(IsDigitChar(c)) {
			StringBufferPush(&(tokenizer->buffer), c);
			tokenizer->isValid = true;
			return true;
		}
		if(IsWhiteSpace(c) || (c == 0)) {
			// whitespace terminates number
			tokenizer->data._register.index = StringToInt64(
					tokenizer->buffer.buffer, tokenizer->buffer.stringLength);
			tokenizer->isFull = true;
			return true;
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


void TokenizerReset(Tokenizer * tokenizer)
{
	StringBuffer buffer = tokenizer->buffer;
	SetMemory(tokenizer, sizeof(Tokenizer), 0);
	tokenizer->buffer = buffer;
	StringBufferReset(&(tokenizer->buffer));
}


void TokenizerCleanup(Tokenizer * tokenizer)
{
	StringBufferCleanup(&(tokenizer->buffer));
}


static Atom parseFloat(char const * syntax, size32 length)
{
	return CreateFloat64(StringToFloat64(syntax, length));
}


static Atom parseInteger(char const * syntax, size32 length)
{
	return CreateInt(StringToInt64(syntax, length));
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
		token.atom = CreateString(string, stringLength);
		break;

	case TOKEN_NUMBER:
		if(StringContainsChar(string, stringLength, '.'))
			token.atom = parseFloat(string, stringLength);
		else
			token.atom = parseInteger(string, stringLength);
		break;

	case TOKEN_VARIABLE:
		// NOTE: variable names must now be a single char
		if(stringLength == 0)
			token.atom = anonymousVariable;
		else
			token.atom = CreateVariable(string[0]);
		break;

	case TOKEN_IN_PARAMETER:
	case TOKEN_OUT_PARAMETER:
		ASSERT(tokenizer->data.parameter.index > 0)
		ASSERT(tokenizer->data.parameter.index <= 255)

		token.atom = CreateParameter(
			tokenizer->data.parameter.index,
			tokenizer->type == TOKEN_IN_PARAMETER ? PARAMETER_IN : PARAMETER_OUT,
			tokenizer->data.parameter.type
		);
		break;

	case TOKEN_REGISTER: {
		ASSERT(tokenizer->data._register.index > 0)
		ASSERT(tokenizer->data._register.index <= 255)
		token.atom = CreateRegister(tokenizer->data._register.index);
		break;
	}
				
	case TOKEN_NAME:
		token.atom = CreateName(string, stringLength);
		break;

	default:
		// tokens that do not represent an atom
		token.atom = invalidAtom;
	}
	return token;
}


Token CreateTokenFromCString(char const * cString)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);
	char const * p = cString;
	while(*p) {
		ASSERT(TokenizerPush(&tokenizer, *p++));
		if(tokenizer.isFull)
			break;	// token complete before string ends
	}
	Token token = TokenizerGetToken(&tokenizer);
	TokenizerCleanup(&tokenizer);
	return token;
}
