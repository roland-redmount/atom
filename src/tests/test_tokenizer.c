
#include "kernel/float.h"
#include "kernel/Int.h"
#include "lang/Variable.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/list.h"
#include "kernel/Parameter.h"
#include "parser/Tokenizer.h"
#include "testing/testing.h"


static void testPushString(Tokenizer * tokenizer, char const * string, size32 length)
{
	for(index32 i = 0; i < length; i++) {
		ASSERT_TRUE(TokenizerPush(tokenizer, string[i]))
		ASSERT_FALSE(TokenizerComplete(tokenizer))
	}
}

// push each character of a C string, excluding zero terminator
static void pushCString(Tokenizer * tokenizer, char const * string)
{
	testPushString(tokenizer, string, CStringLength(string));
}


static Token tokenizeCString(Tokenizer * tokenizer, char const * string)
{
	pushCString(tokenizer, string);
	ASSERT_TRUE(TokenizerPush(tokenizer, 0))
	ASSERT_TRUE(TokenizerComplete(tokenizer))
	Token token = TokenizerGetToken(tokenizer);
	TokenizerReset(tokenizer);
	return token;
}


static Token testTokenizeCharacter(Tokenizer * tokenizer, char c)
{
	ASSERT_TRUE(TokenizerPush(tokenizer, c))
	ASSERT_TRUE(TokenizerComplete(tokenizer))
	Token token = TokenizerGetToken(tokenizer);
	TokenizerReset(tokenizer);
	return token;
}


// TODO: move this
static void testStringBuffer(void)
{
	char const * exampleString = "foobar";
	size32 exampleStringLength = CStringLength(exampleString);

	StringBuffer buffer;
	StringBufferInit(&buffer);
	for(index32 i = 0; i < exampleStringLength; i++)
		StringBufferPush(&buffer, exampleString[i]);

	ASSERT_MEMORY_EQUAL(buffer.buffer, exampleString, buffer.stringLength)
	StringBufferCleanup(&buffer);
}


static void testTokenizer(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);
	Token token;

	// single-character tokens
	token = testTokenizeCharacter(&tokenizer, '&');
	ASSERT_UINT32_EQUAL(TOKEN_AND, token.type)

	token = testTokenizeCharacter(&tokenizer, '|');
	ASSERT_UINT32_EQUAL(token.type, TOKEN_OR)

	token = testTokenizeCharacter(&tokenizer, '!');
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NOT)

	// a name token
	char const * nameString = "foobar";
	token = tokenizeCString(&tokenizer, nameString);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_NAME)
	ReleaseToken(token);

	// test string "foobar" enclosed in ""
	ASSERT_TRUE(TokenizerPush(&tokenizer, '"'))
	pushCString(&tokenizer, nameString);
	ASSERT_TRUE(TokenizerPush(&tokenizer, '"'))
	ASSERT_TRUE(TokenizerComplete(&tokenizer))
	token = TokenizerGetToken(&tokenizer);
	TokenizerReset(&tokenizer);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_STRING)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_ID)
	Atom tokenString = token.typedAtom.atom;
	ASSERT_UINT32_EQUAL(ListLength(tokenString), 6)
	for(index32 i = 0; i < 6; i++) {
		Atom letter = ListGetElement(tokenString, i+1);
		ASSERT_CHAR_EQUAL(LetterToChar(letter, LETTER_LOWERCASE), nameString[i])
	}
	ReleaseToken(token);

	char const * integerString = "12345";
	token = tokenizeCString(&tokenizer, integerString);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_INT)
	ASSERT_INT64_EQUAL(token.typedAtom.atom._int, 12345);

	integerString = "0";
	token = tokenizeCString(&tokenizer, integerString);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_INT)
	ASSERT_INT64_EQUAL(token.typedAtom.atom._int, 0);

	char const * decimalString = "123.45";
	token = tokenizeCString(&tokenizer, decimalString);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_FLOAT)
	ASSERT_FLOAT_EQUAL(token.typedAtom.atom._float, 123.45)

	// the string "123.45." is not a legal number. The tokenizer stays incomplete,
	// which is what tells a syntax error from a token ended by a separator.
	pushCString(&tokenizer, decimalString);
	ASSERT_FALSE(TokenizerPush(&tokenizer, '.'))
	ASSERT_FALSE(TokenizerComplete(&tokenizer))
	TokenizerReset(&tokenizer);

	// a variable
	ASSERT_TRUE(TokenizerPush(&tokenizer, '_'))
	ASSERT_TRUE(TokenizerPush(&tokenizer, 'v'))
	ASSERT_TRUE(TokenizerComplete(&tokenizer))
	token = TokenizerGetToken(&tokenizer);
	TokenizerReset(&tokenizer);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.typedAtom.atom), 'v');

	TokenizerCleanup(&tokenizer);
}


/**
 * A parameter is written as a number, an io direction and an atom type name,
 * which is the notation a service signature is registered in;
 * see RegisterMachineService()
 */
static void testTokenizeParameter(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);

	Token token = tokenizeCString(&tokenizer, "@1<INT");
	ASSERT_UINT32_EQUAL(token.type, TOKEN_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 1)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.io, PARAMETER_IN)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.atomType, AT_INT)

	token = tokenizeCString(&tokenizer, "@2>ID");
	ASSERT_UINT32_EQUAL(token.type, TOKEN_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 2)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.io, PARAMETER_OUT)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.atomType, AT_ID)

	// a parameter number of more than one digit
	token = tokenizeCString(&tokenizer, "@12<UINT");
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 12)

	// A parameter number is 1-based, so @0 is not a parameter
	ASSERT_TRUE(TokenizerPush(&tokenizer, '@'))
	ASSERT_TRUE(TokenizerPush(&tokenizer, '0'))
	ASSERT_FALSE(TokenizerPush(&tokenizer, '<'))
	TokenizerReset(&tokenizer);

	// neither is a parameter without a number
	ASSERT_TRUE(TokenizerPush(&tokenizer, '@'))
	ASSERT_FALSE(TokenizerPush(&tokenizer, '<'))
	TokenizerReset(&tokenizer);

	TokenizerCleanup(&tokenizer);
}


static void testCreateTokenFromCString(void)
{
	Token token = CreateTokenFromCString("_x");
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.typedAtom.atom), 'x');
	ReleaseToken(token);

	// A token running to the end of the string is completed by the terminator.
	// The atom type of a parameter comes last, so it is what a missing one loses.
	token = CreateTokenFromCString("@1<INT");
	ASSERT_UINT32_EQUAL(token.type, TOKEN_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 1)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.io, PARAMETER_IN)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.atomType, AT_INT)
	ReleaseToken(token);

	token = CreateTokenFromCString("foobar");
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_NAME)
	ReleaseToken(token);

	// a token ending in a character of its own completes without the terminator
	token = CreateTokenFromCString("&");
	ASSERT_UINT32_EQUAL(token.type, TOKEN_AND)
	ReleaseToken(token);
}


/**
 * Push a string followed by a separator character. The separator ends the token
 * without being part of it, so the token is taken here and the separator is left
 * for the caller to push again. See TokenizerPush().
 */
static Token takeTerminatedToken(Tokenizer * tokenizer, char const * string, char separator)
{
	pushCString(tokenizer, string);
	ASSERT_FALSE(TokenizerPush(tokenizer, separator))
	ASSERT_TRUE(TokenizerComplete(tokenizer))
	Token token = TokenizerGetToken(tokenizer);
	TokenizerReset(tokenizer);
	return token;
}


static void testSeparatorTerminatesToken(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);

	// a name closing a reflection, as in [foo bar]
	Token token = takeTerminatedToken(&tokenizer, "bar", ']');
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);
	token = testTokenizeCharacter(&tokenizer, ']');
	ASSERT_UINT32_EQUAL(token.type, TOKEN_END_REFLECT)
	ReleaseToken(token);

	// a number followed by a conjunction
	token = takeTerminatedToken(&tokenizer, "12", '&');
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_INT64_EQUAL(token.typedAtom.atom._int, 12);
	ReleaseToken(token);
	token = testTokenizeCharacter(&tokenizer, '&');
	ASSERT_UINT32_EQUAL(token.type, TOKEN_AND)
	ReleaseToken(token);

	TokenizerCleanup(&tokenizer);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testStringBuffer);
	ExecuteTest(testTokenizer);
	ExecuteTest(testTokenizeParameter);
	ExecuteTest(testCreateTokenFromCString);
	ExecuteTest(testSeparatorTerminatesToken);

	KernelShutdown();

	TestSummary();
}


