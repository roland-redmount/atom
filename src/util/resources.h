#ifndef RESOURCES_H
#define RESOURCES_H


void GetResourceDirectory(char * buffer, size32 bufferSize);

void GetResourceFilePath(char const * resourceFileName, char * buffer, size32 bufferSize);

void ToAbsolutePath(char * relativePath, index32 bufferSize);


#endif	// RESOURCES_H
