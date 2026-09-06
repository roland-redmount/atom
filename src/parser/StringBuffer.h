/**
 * A simple string buffer that grows as needed, based on Reallocate().
 */

#ifndef STRINGBUFFER_H
#define STRINGBUFFER_H

#include "platform.h"


typedef struct s_StringBuffer {
	char * buffer;
	size32 bufferSize;
	index32 stringLength;
} StringBuffer;

/**
 * Setup a string buffer.
 */
void StringBufferInit(StringBuffer * buffer);

/**
 * Push a character to the string buffer.
 */
void StringBufferPush(StringBuffer * buffer, char c);

/**
 * Reset the string buffer to zero length.
 */
void StringBufferReset(StringBuffer * buffer);

/**
 * Deallocate the string buffer.
 */
void StringBufferFree(StringBuffer * buffer);


#endif	// STRINGBUFFER_H
