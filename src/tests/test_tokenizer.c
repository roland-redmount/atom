
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


static Token tokenizeCString(Tokenizer * tokenizer, char const * string, enum TokenizerMode mode)
{
	TokenizerSetMode(tokenizer, mode);
	pushCString(tokenizer, string);
	ASSERT_TRUE(TokenizerPush(tokenizer, 0))
	ASSERT_TRUE(TokenizerComplete(tokenizer))
	Token token = TokenizerGetToken(tokenizer);
	TokenizerReset(tokenizer);
	return token;
}


static Token testTokenizeCharacter(Tokenizer * tokenizer, char c, enum TokenizerMode mode)
{
	TokenizerSetMode(tokenizer, mode);
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

	// single-character tokens, all of them standing where a role name may
	token = testTokenizeCharacter(&tokenizer, '&', TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(TOKEN_AND, token.type)

	token = testTokenizeCharacter(&tokenizer, '|', TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_OR)

	token = testTokenizeCharacter(&tokenizer, '!', TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NOT)

	// a name token
	char const * nameString = "foobar";
	token = tokenizeCString(&tokenizer, nameString, TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_NAME)
	ReleaseToken(token);

	// test string "foobar" enclosed in ""
	TokenizerSetMode(&tokenizer, TOKENIZER_ACTOR_MODE);
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
	token = tokenizeCString(&tokenizer, integerString, TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_INT)
	ASSERT_INT64_EQUAL(token.typedAtom.atom._int, 12345);

	integerString = "0";
	token = tokenizeCString(&tokenizer, integerString, TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_INT)
	ASSERT_INT64_EQUAL(token.typedAtom.atom._int, 0);

	char const * decimalString = "123.45";
	token = tokenizeCString(&tokenizer, decimalString, TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_FLOAT)
	ASSERT_FLOAT_EQUAL(token.typedAtom.atom._float, 123.45)

	// the string "123.45." is not a legal number. The tokenizer stays incomplete,
	// which is what tells a syntax error from a token ended by a separator.
	TokenizerSetMode(&tokenizer, TOKENIZER_ACTOR_MODE);
	pushCString(&tokenizer, decimalString);
	ASSERT_FALSE(TokenizerPush(&tokenizer, '.'))
	ASSERT_FALSE(TokenizerComplete(&tokenizer))
	TokenizerReset(&tokenizer);

	// a variable, which is a bare letter where an actor stands
	token = tokenizeCString(&tokenizer, "v", TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.typedAtom.atom), 'v');

	// the anonymous variable, which names no variable at all
	token = tokenizeCString(&tokenizer, "_", TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.typedAtom.atom), '_');

	TokenizerCleanup(&tokenizer);
}


/**
 * A letter of the alphabet is written 'A, with no closing quote since a letter is one
 * character. It is an actor, and stands nowhere a role name does.
 */
static void testTokenizeLetter(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);

	Token token = tokenizeCString(&tokenizer, "'A", TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_LETTER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_LETTER)
	ASSERT_CHAR_EQUAL(LetterToChar(token.typedAtom.atom, LETTER_UPPERCASE), 'A')

	// a letter is case-insensitive, so 'a is the same atom as 'A
	Token lowerToken = tokenizeCString(&tokenizer, "'a", TOKENIZER_ACTOR_MODE);
	ASSERT_TRUE(SameTypedAtoms(lowerToken.typedAtom, token.typedAtom))

	// A letter is one character, so a second one is an error rather than the start of
	// the next token; see TOKEN_VARIABLE for the same rule on a variable name.
	TokenizerSetMode(&tokenizer, TOKENIZER_ACTOR_MODE);
	ASSERT_TRUE(TokenizerPush(&tokenizer, '\''))
	ASSERT_TRUE(TokenizerPush(&tokenizer, 'A'))
	ASSERT_FALSE(TokenizerPush(&tokenizer, 'B'))
	ASSERT_FALSE(TokenizerComplete(&tokenizer))
	TokenizerReset(&tokenizer);

	// only a letter of the alphabet follows the quote
	TokenizerSetMode(&tokenizer, TOKENIZER_ACTOR_MODE);
	ASSERT_TRUE(TokenizerPush(&tokenizer, '\''))
	ASSERT_FALSE(TokenizerPush(&tokenizer, '4'))
	TokenizerReset(&tokenizer);

	// and a letter stands nowhere a role name does
	TokenizerSetMode(&tokenizer, TOKENIZER_ROLE_MODE);
	ASSERT_FALSE(TokenizerPush(&tokenizer, '\''))
	TokenizerReset(&tokenizer);

	TokenizerCleanup(&tokenizer);
}


/**
 * The mode decides what a character may begin, which is what lets a bare word be a role
 * name in one place and a variable in the other; see enum TokenizerMode.
 */
static void testTokenizerMode(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);

	// the same word is a name where a role stands and a variable where an actor does
	Token token = tokenizeCString(&tokenizer, "x", TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);
	token = tokenizeCString(&tokenizer, "x", TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)

	// A variable is named by a single letter, so a second one is an error rather than
	// the start of the next token. The tokenizer stays incomplete, which is what tells
	// the two apart; see TokenizerPush().
	TokenizerSetMode(&tokenizer, TOKENIZER_ACTOR_MODE);
	ASSERT_TRUE(TokenizerPush(&tokenizer, 'x'))
	ASSERT_FALSE(TokenizerPush(&tokenizer, 'y'))
	ASSERT_FALSE(TokenizerComplete(&tokenizer))
	TokenizerReset(&tokenizer);

	// a name may hold characters that name no variable, and none of them begins an actor
	token = tokenizeCString(&tokenizer, "+", TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);
	TokenizerSetMode(&tokenizer, TOKENIZER_ACTOR_MODE);
	ASSERT_FALSE(TokenizerPush(&tokenizer, '+'))
	TokenizerReset(&tokenizer);

	// a number is an actor, and stands nowhere a role name does
	TokenizerSetMode(&tokenizer, TOKENIZER_ROLE_MODE);
	ASSERT_FALSE(TokenizerPush(&tokenizer, '4'))
	TokenizerReset(&tokenizer);

	// a reflection opens where an actor stands and closes where a role name does
	TokenizerSetMode(&tokenizer, TOKENIZER_ROLE_MODE);
	ASSERT_FALSE(TokenizerPush(&tokenizer, '['))
	TokenizerReset(&tokenizer);
	TokenizerSetMode(&tokenizer, TOKENIZER_ACTOR_MODE);
	ASSERT_FALSE(TokenizerPush(&tokenizer, ']'))
	TokenizerReset(&tokenizer);

	TokenizerCleanup(&tokenizer);
}


/**
 * Resetting a tokenizer on a completed token leaves it in the mode the token after it is
 * read in: a role name is followed by its actor, and everything else by a role name.
 */
static void testModeFollowsToken(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);

	// a formula begins with a role name
	Token token = tokenizeCString(&tokenizer, "foo", TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);

	// the name put the tokenizer where its actor is read
	pushCString(&tokenizer, "y");
	ASSERT_TRUE(TokenizerPush(&tokenizer, 0))
	token = TokenizerGetToken(&tokenizer);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	TokenizerReset(&tokenizer);

	// and the actor put it back where the next role name is read
	pushCString(&tokenizer, "bar");
	ASSERT_TRUE(TokenizerPush(&tokenizer, 0))
	token = TokenizerGetToken(&tokenizer);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);
	TokenizerReset(&tokenizer);

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

	Token token = tokenizeCString(&tokenizer, "@1<INT", TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 1)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.io, PARAMETER_IN)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.atomType, AT_INT)

	token = tokenizeCString(&tokenizer, "@2>ID", TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 2)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.io, PARAMETER_OUT)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.atomType, AT_ID)

	// a parameter number of more than one digit
	token = tokenizeCString(&tokenizer, "@12<UINT", TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 12)

	// A parameter number is 1-based, so @0 is not a parameter
	TokenizerSetMode(&tokenizer, TOKENIZER_ACTOR_MODE);
	ASSERT_TRUE(TokenizerPush(&tokenizer, '@'))
	ASSERT_TRUE(TokenizerPush(&tokenizer, '0'))
	ASSERT_FALSE(TokenizerPush(&tokenizer, '<'))
	TokenizerReset(&tokenizer);

	// neither is a parameter without a number
	TokenizerSetMode(&tokenizer, TOKENIZER_ACTOR_MODE);
	ASSERT_TRUE(TokenizerPush(&tokenizer, '@'))
	ASSERT_FALSE(TokenizerPush(&tokenizer, '<'))
	TokenizerReset(&tokenizer);

	TokenizerCleanup(&tokenizer);
}


static void testCreateTokenFromCString(void)
{
	Token token = CreateTokenFromCString("x", TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.typedAtom.atom), 'x');
	ReleaseToken(token);

	// A token running to the end of the string is completed by the terminator.
	// The atom type of a parameter comes last, so it is what a missing one loses.
	token = CreateTokenFromCString("@1<INT", TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 1)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.io, PARAMETER_IN)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.atomType, AT_INT)
	ReleaseToken(token);

	token = CreateTokenFromCString("foobar", TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_NAME)
	ReleaseToken(token);

	// a token ending in a character of its own completes without the terminator
	token = CreateTokenFromCString("&", TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_AND)
	ReleaseToken(token);
}


/**
 * Push a string followed by a separator character. The separator ends the token
 * without being part of it, so the token is taken here and the separator is left
 * for the caller to push again. See TokenizerPush().
 */
static Token takeTerminatedToken(
	Tokenizer * tokenizer, char const * string, char separator, enum TokenizerMode mode)
{
	TokenizerSetMode(tokenizer, mode);
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

	// a name followed by a disjunction, as in [foo 1 | bar 2]
	Token token = takeTerminatedToken(&tokenizer, "bar", '|', TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);
	token = testTokenizeCharacter(&tokenizer, '|', TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_OR)
	ReleaseToken(token);

	// a variable closing a reflection, as in [foo x]
	token = takeTerminatedToken(&tokenizer, "x", ']', TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	ReleaseToken(token);
	token = testTokenizeCharacter(&tokenizer, ']', TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_END_REFLECT)
	ReleaseToken(token);

	// a number followed by a conjunction
	token = takeTerminatedToken(&tokenizer, "12", '&', TOKENIZER_ACTOR_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_INT64_EQUAL(token.typedAtom.atom._int, 12);
	ReleaseToken(token);
	token = testTokenizeCharacter(&tokenizer, '&', TOKENIZER_ROLE_MODE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_AND)
	ReleaseToken(token);

	TokenizerCleanup(&tokenizer);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testStringBuffer);
	ExecuteTest(testTokenizer);
	ExecuteTest(testTokenizeLetter);
	ExecuteTest(testTokenizerMode);
	ExecuteTest(testModeFollowsToken);
	ExecuteTest(testTokenizeParameter);
	ExecuteTest(testCreateTokenFromCString);
	ExecuteTest(testSeparatorTerminatesToken);

	KernelShutdown();

	TestSummary();
}


