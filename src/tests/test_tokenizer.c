
#include "kernel/float.h"
#include "kernel/ifact.h"
#include "kernel/Int.h"
#include "lang/Variable.h"
#include "kernel/kernel.h"
#include "kernel/letter.h"
#include "library/library.h"
#include "library/list.h"
#include "library/string.h"
#include "kernel/Parameter.h"
#include "parser/Tokenizer.h"
#include "testing/testing.h"


// push each character of a C string, excluding zero terminator
static void pushCString(Tokenizer * tokenizer, char const * string)
{
	size32 length = CStringLength(string);
	for(index32 i = 0; i < length; i++)
		ASSERT_UINT32_EQUAL(TokenizerPush(tokenizer, string[i]), TOKENIZER_ACCEPTED)
}


static Token tokenizeCString(Tokenizer * tokenizer, char const * string, enum TokenizerState mode)
{
	TokenizerRestart(tokenizer, mode);
	pushCString(tokenizer, string);
	ASSERT_UINT32_EQUAL(TokenizerPush(tokenizer, 0), TOKENIZER_ACCEPTED)
	ASSERT_TRUE(TokenizerIsFull(tokenizer))
	Token token = TokenizerGetToken(tokenizer);
	TokenizerReset(tokenizer);
	return token;
}


static Token testTokenizeCharacter(Tokenizer * tokenizer, char c, enum TokenizerState mode)
{
	TokenizerRestart(tokenizer, mode);
	ASSERT_UINT32_EQUAL(TokenizerPush(tokenizer, c), TOKENIZER_ACCEPTED)
	ASSERT_TRUE(TokenizerIsFull(tokenizer))
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
	StringBufferFree(&buffer);
}


static void testTokenizer(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);
	Token token;

	// single-character tokens, all of them standing where a role name may
	token = testTokenizeCharacter(&tokenizer, '&', TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(TOKEN_AND, token.type)

	token = testTokenizeCharacter(&tokenizer, '|', TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_OR)

	token = testTokenizeCharacter(&tokenizer, '!', TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NOT)

	// a name token
	char const * nameString = "foobar";
	token = tokenizeCString(&tokenizer, nameString, TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_NAME)
	ReleaseToken(token);

	// test string "foobar" enclosed in ""
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '"'), TOKENIZER_ACCEPTED)
	pushCString(&tokenizer, nameString);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '"'), TOKENIZER_ACCEPTED)
	ASSERT_TRUE(TokenizerIsFull(&tokenizer))
	token = TokenizerGetToken(&tokenizer);
	TokenizerReset(&tokenizer);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_STRING)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_ID)
	Atom tokenString = token.typedAtom.atom;
	ASSERT_UINT32_EQUAL(ListLength(tokenString), 6)
	for(index32 i = 0; i < 6; i++) {
		Atom letter = ListGetElement(tokenString, i+1);
		ASSERT_CHAR_EQUAL(LetterToChar(letter), nameString[i])
	}
	ReleaseToken(token);

	// a string holds ASCII letters only, and at least one letter
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '"'), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 'a'), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '1'), TOKENIZER_REJECTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, ' '), TOKENIZER_REJECTED)
	// the first byte of the UTF-8 character å
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, (char) 0xC3), TOKENIZER_REJECTED)

	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '"'), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '"'), TOKENIZER_REJECTED)

	char const * integerString = "12345";
	token = tokenizeCString(&tokenizer, integerString, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_INT)
	ASSERT_INT64_EQUAL(token.typedAtom.atom._int, 12345);

	integerString = "0";
	token = tokenizeCString(&tokenizer, integerString, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_INT)
	ASSERT_INT64_EQUAL(token.typedAtom.atom._int, 0);

	char const * decimalString = "123.45";
	token = tokenizeCString(&tokenizer, decimalString, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_FLOAT)
	ASSERT_FLOAT_EQUAL(token.typedAtom.atom._float, 123.45)

	// CLAUDE: a negative number begins with a minus sign
	token = tokenizeCString(&tokenizer, "-42", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_INT)
	ASSERT_INT64_EQUAL(token.typedAtom.atom._int, -42);

	token = tokenizeCString(&tokenizer, "-1.5", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_FLOAT)
	ASSERT_FLOAT_EQUAL(token.typedAtom.atom._float, -1.5)

	token = tokenizeCString(&tokenizer, "-0", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_INT)
	ASSERT_INT64_EQUAL(token.typedAtom.atom._int, 0);

	// CLAUDE: the minus sign must be followed directly by a digit
	char const notDigits[] = {' ', 0, '.', '-'};
	for(index8 i = 0; i < sizeof(notDigits); i++) {
		TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
		ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '-'), TOKENIZER_ACCEPTED)
		ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, notDigits[i]), TOKENIZER_REJECTED)
	}

	// CLAUDE: where a role name stands, the minus sign is a name, as in (+ x - y = z)
	token = tokenizeCString(&tokenizer, "-", TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);

	// the string "123.45." is not a legal number. The tokenizer stays incomplete,
	// which is what tells a syntax error from a token ended by a separator.
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	pushCString(&tokenizer, decimalString);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '.'), TOKENIZER_REJECTED)
	ASSERT_FALSE(TokenizerIsFull(&tokenizer))

	// a variable, which is a single letter
	token = tokenizeCString(&tokenizer, "v", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.typedAtom.atom), 'v');
	ASSERT_FALSE(token.typedAtom.atom.variable.quoted)

	// the anonymous variable, which names no variable at all
	token = tokenizeCString(&tokenizer, "_", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.typedAtom.atom), '_');
	ASSERT_FALSE(token.typedAtom.atom.variable.quoted)

	// a quoted variable
	token = tokenizeCString(&tokenizer, "^v", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.typedAtom.atom), 'v');
	ASSERT_TRUE(token.typedAtom.atom.variable.quoted)

	TokenizerFree(&tokenizer);
}


/**
 * A letter of the alphabet is written 'A, with no closing quote since a letter is one
 * character. It is an actor, and stands nowhere a role name does.
 */
static void testTokenizeLetter(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);

	Token token = tokenizeCString(&tokenizer, "'A", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_LETTER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_LETTER)
	ASSERT_CHAR_EQUAL(LetterToChar(token.typedAtom.atom), 'A')

	// Letters are now case-sensitive
	Token lowerToken = tokenizeCString(&tokenizer, "'a", TOKENIZER_ACTOR_STATE);
	ASSERT_FALSE(SameTypedAtoms(lowerToken.typedAtom, token.typedAtom))

	// A letter is one character, so a second one is an error rather than the start of
	// the next token; see TOKEN_VARIABLE for the same rule on a variable name.
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '\''), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 'A'), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 'B'), TOKENIZER_REJECTED)

	// only a letter of the alphabet follows the quote
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '\''), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '4'), TOKENIZER_REJECTED)

	// and a letter stands nowhere a role name does
	TokenizerRestart(&tokenizer, TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '\''), TOKENIZER_REJECTED)

	TokenizerFree(&tokenizer);
}


/**
 * The tokenizer state decides what tokens are legal. See enum TokenizerState.
 */
static void testTokenizerState(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);

	// "x" represents a name in TOKENIZER_ROLE_STATE, but and a variable in TOKENIZER_ACTOR_STATE
	Token token = tokenizeCString(&tokenizer, "x", TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);
	token = tokenizeCString(&tokenizer, "x", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)

	// A variable is named by a single letter, so pushing a second character
	// is an error rather than the start of the next token; see enum TokenizerInputMode.
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 'x'), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 'y'), TOKENIZER_REJECTED)

	// a name may hold characters that name no variable, and none of them begins an actor
	token = tokenizeCString(&tokenizer, "+", TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '+'), TOKENIZER_REJECTED)

	// a number is an actor, and stands nowhere a role name does
	TokenizerRestart(&tokenizer, TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '4'), TOKENIZER_REJECTED)

	// CLAUDE: a ] closes a reflection where a role name stands, and a reflected name [name]
	// where an actor stands
	token = testTokenizeCharacter(&tokenizer, ']', TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_END_REFLECT)

	TokenizerFree(&tokenizer);
}


/**
 * Resetting a tokenizer on a completed token leaves it in the mode the token after it is
 * read in: a role name is followed by its actor, and everything else by a role name.
 */
static void testModeFollowsToken(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);

	// a formula begins with a role name
	Token token = tokenizeCString(&tokenizer, "foo", TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);

	// the name put the tokenizer where its actor is read
	pushCString(&tokenizer, "y");
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 0), TOKENIZER_ACCEPTED)
	token = TokenizerGetToken(&tokenizer);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	TokenizerReset(&tokenizer);

	// and the actor put it back where the next role name is read
	pushCString(&tokenizer, "bar");
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 0), TOKENIZER_ACCEPTED)
	token = TokenizerGetToken(&tokenizer);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);
	TokenizerReset(&tokenizer);

	TokenizerFree(&tokenizer);
}


/**
 * A parameter is written as a number, an io direction and an atom type name,
 * which is the notation a service signature is registered in;
 * see RegisterMachineService()
 */
static void testTokenizeParameter(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);

	Token token = tokenizeCString(&tokenizer, "#1<INT", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 1)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.io, PARAMETER_IN)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.atomType, AT_INT)

	token = tokenizeCString(&tokenizer, "#2>ID", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 2)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.io, PARAMETER_OUT)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.atomType, AT_ID)

	// a parameter number of more than one digit
	token = tokenizeCString(&tokenizer, "#12<NAME", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 12)

	// A parameter number is 1-based, so #0 is not a parameter
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '#'), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '0'), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '<'), TOKENIZER_REJECTED)

	// neither is a parameter without a number
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '#'), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '<'), TOKENIZER_REJECTED)

	TokenizerFree(&tokenizer);
}


/*
 * CLAUDE: An AT_ID atom is written as @ followed by the ID_HASH_LENGTH hex digits of its hash.
 */
static void testTokenizeID(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);
	Atom string = CreateStringFromCString("abc");
	char idString[ID_HASH_LENGTH + 2];
	FormatString(idString, sizeof(idString), "@%016llx", (unsigned long long) string.hash);

	// the token holds a reference to the AT_ID atom until the token is released
	uint32 referenceCount = IFactReferenceCount(string);
	Token token = tokenizeCString(&tokenizer, idString, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_ID)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_ID)
	ASSERT_UINT64_EQUAL(token.typedAtom.atom.hash, string.hash)
	ASSERT_UINT32_EQUAL(IFactReferenceCount(string), referenceCount + 1)
	ReleaseToken(token);
	ASSERT_UINT32_EQUAL(IFactReferenceCount(string), referenceCount)

	// a separator completes the token, and is left for the next token
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	pushCString(&tokenizer, idString);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, ']'), TOKENIZER_ENDED)
	ReleaseToken(TokenizerGetToken(&tokenizer));

	// a hash with a digit missing
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	for(index32 i = 0; i < ID_HASH_LENGTH; i++)
		ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, idString[i]), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 0), TOKENIZER_REJECTED)

	// a hash with a digit too many
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	pushCString(&tokenizer, idString);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, '0'), TOKENIZER_REJECTED)

	// a hash naming no stored AT_ID atom
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	pushCString(&tokenizer, "@0000000000000000");
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 0), TOKENIZER_REJECTED)

	IFactRelease(string);
	TokenizerFree(&tokenizer);
}


static void testCreateTokenFromCString(void)
{
	Token token = CreateTokenFromCString("x", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.typedAtom.atom), 'x');
	ReleaseToken(token);

	// A token running to the end of the string is completed by the terminator.
	// The atom type of a parameter comes last, so it is what a missing one loses.
	token = CreateTokenFromCString("#1<INT", TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_PARAMETER)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.number, 1)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.io, PARAMETER_IN)
	ASSERT_UINT32_EQUAL(token.typedAtom.atom.parameter.atomType, AT_INT)
	ReleaseToken(token);

	token = CreateTokenFromCString("foobar", TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ASSERT_UINT32_EQUAL(token.typedAtom.type, AT_NAME)
	ReleaseToken(token);

	// a token ending in a character of its own completes without the terminator
	token = CreateTokenFromCString("&", TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_AND)
	ReleaseToken(token);
}


/**
 * Push a string followed by a separator character. The separator ends the token
 * without being part of it, so the token is taken here and the separator is left
 * for the caller to push again. See TokenizerPush().
 */
static Token takeTerminatedToken(
	Tokenizer * tokenizer, char const * string, char separator, enum TokenizerState mode)
{
	TokenizerRestart(tokenizer, mode);
	pushCString(tokenizer, string);
	ASSERT_UINT32_EQUAL(TokenizerPush(tokenizer, separator), TOKENIZER_ENDED)
	ASSERT_TRUE(TokenizerIsFull(tokenizer))
	Token token = TokenizerGetToken(tokenizer);
	TokenizerReset(tokenizer);
	return token;
}


static void testSeparatorTerminatesToken(void)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);

	// a name followed by a disjunction, as in [foo 1 | bar 2]
	Token token = takeTerminatedToken(&tokenizer, "bar", '|', TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);
	token = testTokenizeCharacter(&tokenizer, '|', TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_OR)
	ReleaseToken(token);

	// a variable closing a reflection, as in [foo x]
	token = takeTerminatedToken(&tokenizer, "x", ']', TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	ReleaseToken(token);
	token = testTokenizeCharacter(&tokenizer, ']', TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_END_REFLECT)
	ReleaseToken(token);

	// a number followed by a conjunction
	token = takeTerminatedToken(&tokenizer, "12", '&', TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NUMBER)
	ASSERT_INT64_EQUAL(token.typedAtom.atom._int, 12);
	ReleaseToken(token);
	token = testTokenizeCharacter(&tokenizer, '&', TOKENIZER_ROLE_STATE);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_AND)
	ReleaseToken(token);

	TokenizerFree(&tokenizer);
}


/* CLAUDE: Store the type of each token TokenizeCString() reads, for testRelationBrackets(). */
typedef struct {
	enum TokenType types[8];
	size8 nTokens;
} TokenTypeList;

static bool tokenTypeHandler(void * context, Token token)
{
	TokenTypeList * list = (TokenTypeList *) context;
	ASSERT(list->nTokens < 8)
	list->types[list->nTokens++] = token.type;
	return true;
}


/**
 * CLAUDE: A relation [[foo 1]] is read as two TOKEN_BEGIN_REFLECT and two TOKEN_END_REFLECT.
 * The second [ is read in TOKENIZER_ROLE_STATE.
 */
static void testRelationBrackets(void)
{
	TokenTypeList list = {.nTokens = 0};
	TokenizeCString("bar [[foo 1]]", tokenTypeHandler, &list);

	enum TokenType expected[] = {
		TOKEN_NAME, TOKEN_BEGIN_REFLECT, TOKEN_BEGIN_REFLECT,
		TOKEN_NAME, TOKEN_NUMBER, TOKEN_END_REFLECT, TOKEN_END_REFLECT
	};
	ASSERT_UINT32_EQUAL(list.nTokens, 7)
	for(index8 i = 0; i < 7; i++)
		ASSERT_UINT32_EQUAL(list.types[i], expected[i])
}


/* CLAUDE: The ] of a reflected name [foo] is read in TOKENIZER_ACTOR_STATE. */
static void testReflectedNameBrackets(void)
{
	TokenTypeList list = {.nTokens = 0};
	TokenizeCString("bar [foo] baz 1", tokenTypeHandler, &list);

	enum TokenType expected[] = {
		TOKEN_NAME, TOKEN_BEGIN_REFLECT, TOKEN_NAME, TOKEN_END_REFLECT, TOKEN_NAME, TOKEN_NUMBER
	};
	ASSERT_UINT32_EQUAL(list.nTokens, 6)
	for(index8 i = 0; i < 6; i++)
		ASSERT_UINT32_EQUAL(list.types[i], expected[i])
}


/**
 * CLAUDE: A variable ends in a name character, so whether a further name character may
 * follow it depends on where the characters come from; see enum TokenizerInputMode.
 */
static void testTokenizerInput(void)
{
	Tokenizer tokenizer;

	// In string input mode, an additional character after a variable name is rejected
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 'x'), TOKENIZER_ACCEPTED)
	ASSERT_TRUE(TokenizerIsFull(&tokenizer))
	// A second character 'y' is illegal
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 'y'), TOKENIZER_REJECTED)
	TokenizerFree(&tokenizer);

	// In interactive input mode,  an additional character after a variable name is
	// part of the next token, so that "foo x y" can be typed as "foo xy".
	TokenizerInit(&tokenizer, TOKENIZER_INTERACTIVE_INPUT);
	TokenizerRestart(&tokenizer, TOKENIZER_ACTOR_STATE);
	// A first character 'x' becomes a variable
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 'x'), TOKENIZER_ACCEPTED)
	ASSERT_TRUE(TokenizerIsFull(&tokenizer))
	Token token = TokenizerGetToken(&tokenizer);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_VARIABLE)
	ASSERT_CHAR_EQUAL(GetVariableName(token.typedAtom.atom), 'x')
	// A second character 'y' is legal, but not used by the tokenizer
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 'y'), TOKENIZER_ENDED)

	// The tokenizer is now in the name state, so pushing 'y' again begins a role name
	TokenizerReset(&tokenizer);
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 'y'), TOKENIZER_ACCEPTED)
	ASSERT_UINT32_EQUAL(TokenizerPush(&tokenizer, 0), TOKENIZER_ACCEPTED)
	token = TokenizerGetToken(&tokenizer);
	ASSERT_UINT32_EQUAL(token.type, TOKEN_NAME)
	ReleaseToken(token);
	TokenizerFree(&tokenizer);
}


int main(int argc, char * argv[])
{
	KernelInitialize(PERSISTENT_MEMORY);
	LoadLibraries();

	ExecuteTest(testStringBuffer);
	ExecuteTest(testTokenizer);
	ExecuteTest(testTokenizeLetter);
	ExecuteTest(testTokenizerState);
	ExecuteTest(testTokenizerInput);
	ExecuteTest(testModeFollowsToken);
	ExecuteTest(testTokenizeParameter);
	ExecuteTest(testTokenizeID);
	ExecuteTest(testCreateTokenFromCString);
	ExecuteTest(testSeparatorTerminatesToken);
	ExecuteTest(testRelationBrackets);
	ExecuteTest(testReflectedNameBrackets);

	UnloadLibraries();
	KernelShutdown();

	TestSummary();
}


