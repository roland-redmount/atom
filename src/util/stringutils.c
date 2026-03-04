
#include "util/stringutils.h"
#include "platform.h"

// these are currently not being used

/**
 * Concatenate a number of C strings 
 */

/*
char* JoinStrings(size_t nStrings, ...)
{
	va_list args;
 	// determine string lengths
	size_t lengths[nStrings];
	size_t totalLength = 0;
 	va_start(args, nStrings);
 	for(index32 i = 0; i < nStrings; i++) {
 		char* str = va_arg(args, char*);
		lengths[i] = strlen(str);
		totalLength += lengths[i];
	}
	va_end(args);
 	char* joinedString = (char*) malloc(totalLength + 1);
	// concatenate strings
	char* p = joinedString;
	va_start(args, nStrings);
	for(index32 i = 0; i < nStrings; i++) {
		char* str = va_arg(args, char*);
		strcpy(p, str);
		p += lengths[i];
	}
	va_end(args);
	return joinedString;	
}

*/

/**
 * Convert an integer to a string
 */

/*
char* IntToString(int x)
{
	// we always allocate a 22-byte string buffer
	// a 64-bit integer can store 20-digit numbers, plus a
	// possible minus sign, plus 0 terminator = 22
	char* string = (char*) malloc(22);
	FormatString(string, 22, "%d", x);
	return string;
}
*/

/*
void DumpMemoryHex(byte* buffer, size_t length)
{
	byte c;
	for(index32 i = 0; i < length; i++) {
		// print one byte hexadecimal, 0xXX
		//printf("\n(buffer[i] >> 4) = %u\n", (buffer[i] >> 4));
		c = buffer[i] >> 4;
		PrintChar(c > 9 ? 'A' + c - 10 : '0' + c);
		c = buffer[i] & 0xF;
		PrintChar(c > 9 ? 'A' + c - 10 : '0' + c);
		//putc(' ', stdout);
	}
}
*/
