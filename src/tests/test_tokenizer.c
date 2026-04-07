
#include "datumtypes/FloatIEEE754.h"
#include "datumtypes/Int.h"
#include "datumtypes/Variable.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "kernel/list.h"
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
	ASSERT_UINT32_EQUAL(token.atom.type, DT_NAME)
	ReleaseToken(token);

	// test string "foobar" enclosed in ""
	ASSERT_TRUE(TokenizerPush(&tokenizer, '"'))
	pushCString(&tokenizer, nameString);
	ASSERT_TRUE(TokenizerPush(&tokenizer, '"'))
	ASSERT_TRUE(TokenizerComplete(&tokenizer))
	token = TokenizerGetToken(&tokenizer);
	TokenizerReset(&tokenizer);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_STRING)
	ASSERT_UINT32_EQUAL(token.atom.type, DT_ID)
	Datum tokenString = token.atom.datum;
	ASSERT_UINT32_EQUAL(ListLength(tokenString), 6)
	for(index32 i = 0; i < 6; i++) {
		Atom letter = ListGetElement(tokenString, i+1);
		ASSERT_UINT32_EQUAL(letter.type, DT_LETTER)
		ASSERT_CHAR_EQUAL(LetterToChar(letter, LETTER_LOWERCASE), nameString[i])
	}
	ReleaseToken(token);

	char const * integerString = "12345";
	token = tokenizeCString(&tokenizer, integerString);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.atom.type, DT_INT)
	ASSERT_UINT32_EQUAL(GetIntValue(token.atom), 12345);

	integerString = "0";
	token = tokenizeCString(&tokenizer, integerString);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.atom.type, DT_INT)
	ASSERT_UINT32_EQUAL(GetIntValue(token.atom), 0);

	char const * decimalString = "123.45";
	token = tokenizeCString(&tokenizer, decimalString);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.atom.type, DT_FLOAT64)
	ASSERT_UINT32_EQUAL(GetFloat64Value(token.atom), 123.45);

	// the string "123.45." is not a legal number
	pushCString(&tokenizer, decimalString);
	ASSERT_FALSE(TokenizerPush(&tokenizer, '.'))
	TokenizerReset(&tokenizer);

	// a variable
	ASSERT_TRUE(TokenizerPush(&tokenizer, '_'))
	ASSERT_TRUE(TokenizerPush(&tokenizer, 'v'))
	ASSERT_TRUE(TokenizerComplete(&tokenizer))
	token = TokenizerGetToken(&tokenizer);
	TokenizerReset(&tokenizer);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	ASSERT_UINT32_EQUAL(token.atom.type, DT_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.atom), 'v');

	TokenizerCleanup(&tokenizer);
}


static void testCreateTokenFromCString(void)
{
	Token token = CreateTokenFromCString("_x");
	ASSERT_UINT32_EQUAL(token.atom.type, DT_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.atom), 'x');
	ReleaseToken(token);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testStringBuffer);
	ExecuteTest(testTokenizer);
	ExecuteTest(testCreateTokenFromCString);

	KernelShutdown();

	TestSummary();
}


