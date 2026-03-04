/**
 * Path handling
 * 
 * TODO: make this platform independent, now only Linux
 * TODO: we should encapsulate the path strings to ensure
 *   they have been allocated correctly
 */


#include "platform.h"
#include "resources.h"


const char relResourcePath[] = "src/tests/resources";


/**
 * Return the base directory, assuming the executable runs in
 * <base directory>/build/[os]/bin/something.exe
 * 
 * NOTE: this could be initialized once upon system startup and stored in a global variable
 */
static void getBaseDirectory(char * buffer, size32 bufferSize)
{
	GetExecutablePath(buffer, bufferSize);		// <base directory>/build/bin/something.exe
	GetParentDirectory(buffer, bufferSize);		// <base directory>/build/bin/
	GetParentDirectory(buffer, bufferSize);		// <base directory>/build/
	GetParentDirectory(buffer, bufferSize);		// <base directory>
}


void GetResourceDirectory(char * buffer, size32 bufferSize)
{
	char baseDirPath[maxPathLength + 1];
	getBaseDirectory(baseDirPath, maxPathLength + 1);
	FormatString(buffer, bufferSize, "%s/%s", baseDirPath, relResourcePath);
}


void GetResourceFilePath(char const * resourceFileName, char * buffer, size32 bufferSize)
{
	char resourceDirPath[maxPathLength + 1];
	GetResourceDirectory(resourceDirPath, maxPathLength + 1);
	FormatString(buffer, bufferSize, "%s/%s", resourceDirPath, resourceFileName);
}


static void removeTrailingSlash(char * path)
{
	size32 length = CStringLength(path);
	if(path[length - 1] == '/')
		path[length - 1] = 0;
}


static void ensureTrailingSlash(char * path, index32 bufferSize)
{
	index32 i = CStringLength(path);
	if(path[i-1] != '/') {
		ASSERT(i + 2 < bufferSize);
		path[i++] = '/';
		path[i++] = 0;
	}
}


/**
 * Prepend the base directory to a relative path name
 */
void ToAbsolutePath(char * relativePath, index32 bufferSize)
{
	char baseDirPath[maxPathLength + 1];
	getBaseDirectory(baseDirPath, maxPathLength + 1);
	ensureTrailingSlash(baseDirPath, bufferSize);
	CStringPrepend(baseDirPath, relativePath, bufferSize);
}
