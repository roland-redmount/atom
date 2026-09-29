/**
 * Syntax character classes
 */

#ifndef CHARACTERS_H
#define CHARACTERS_H

#include "platform.h"

/**
 * Character classes.
 * See also platform.h
 */

bool IsSyntaxChar(char c);

bool IsFormChar(char c);

bool IsClauseChar(char c);

bool IsPredicateChar(char c);

bool IsRoleChar(char c);

bool IsNameInitialChar(char c);

bool IsNameChar(char c);

/**
 * A separator character ends the token before it and begins a new one.
 * Space is a separator, as are the characters that begin a token of their
 * own: & | ! " ' _ and the reflection brackets [ ].
 */
bool IsSeparatorChar(char c);

bool IsWhiteSpace(char c);

// A digit 0-9 or a lowercase letter a-f, as in the hash of an AT_ID atom
bool IsHexDigitChar(char c);

/**
 * Test if a string is a valid name
 */
bool IsNameString(char const * string, size32 length);

index32 FindCharIndex(char const * string, size32 length, char c);


bool StringContainsChar(char const * string, size32 length, char c);


#endif	// CHARACTERS_H
