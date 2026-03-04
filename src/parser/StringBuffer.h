
#ifndef STRINGBUFFER_H
#define STRINGBUFFER_H

#include "platform.h"


typedef struct s_StringBuffer {
	char * buffer;
	size32 bufferSize;
	index32 stringLength;
} StringBuffer;


void StringBufferInit(StringBuffer * buffer);

void StringBufferPush(StringBuffer * buffer, char c);

/**
 * Reset the string buffer to zero length.
 */
void StringBufferReset(StringBuffer * buffer);

void StringBufferCleanup(StringBuffer * buffer);


#endif	// STRINGBUFFER_H
