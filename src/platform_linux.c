/**
 * This is the platform layer for Linux x86/amd64 systems.
 * 
 * Most of these functions will be different on Windows, and we would have
 * to write a new platform layer. I tried to include both platforms here
 * using conditional compilation with #ifdef statements, but it gets messy.
 * Probably better to make a separate platform_win.c and select the appropriate
 * file in the build system (makefile).
 */

// C standard library includes
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>		// for strtoll
#include <time.h>

// linux specific includes
#include <libgen.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/limits.h>

#include "platform.h"


// C stdlib comparison functions may return result < 0, > 0 or 0.
// Convert this to always return -1, 0, or 1 to fit in one byte
int8 convertStdLibCompareResult(int result)
{
	if(result > 0)
		return 1;
	else if(result < 0)
		return -1;
	else
		return 0;
}


void SetMemory(void * address, size32 size, byte value)
{
	memset(address, value, size);
}


void CopyMemory(void const * source, void * destination, size32 size)
{
	memcpy(destination, source, size);	
}


// similar to CopyMemory, but allows source and destination blocks to overlap
void MoveMemory(void const * source, void * destination, size32 size)
{
	memmove(destination, source, size);
}


int8 CompareMemory(void const * address1, void const * address2, size32 size)
{
	return convertStdLibCompareResult(memcmp(address1, address2, size));
}


/**
 * Get the highest set bit of a 32-bit value x.
 * x must not be zero, or we have undefined behavior
 */
uint8 GetHighestSetBit(data32 x)
{
	return 32 - __builtin_clz(x);
}


//-------------------------- strings ------------------------------


int64 StringToInt64(char const * string, size32 length)
{
	char intString[length + 1];
	CopyMemory(string, intString, length);
	intString[length] = 0;
	return strtoll(intString, NULL, 10);
}


float64 StringToFloat64(char const * string, size32 length)
{
	// parse using atof() and create atom
	char floatString[length + 1];
	CopyMemory(string, floatString, length);
	floatString[length] = '\0';
	return atof(floatString);
}


size32 CStringLength(char const * string)
{
	return strlen(string);
}


int8 CStringCompare(char const * string1, char const * string2)
{
	return convertStdLibCompareResult(strcmp(string1, string2));
}


int8 CStringCompareLimited(char const * string1, char const * string2, size32 maxLength)
{
	return convertStdLibCompareResult(strncmp(string1, string2, maxLength));
}


void CStringCopy(char const * source, char * destination)
{
	strcpy(destination, source);
}


void CStringCopyLimited(char const * source, char * destination, size32 maxLength)
{
	strncpy(destination, source, maxLength);
}


char const * CStringFindChar(char const * string, char c)
{
	return strchr(string, c);
}


//---------------------------------- printing -----------------------------------

/**
 * TODO: all print calls should target a specific IO stream,
 * either stdout, stderr or a logging stream,
 * and the current stream should be switchable at any time
 * with a SetOutputStream() function.
 * We should also have a stream that captures output into a buffer,
 * so that we can unit test print methods. Currently a large % of code
 * lacking test coverage are print methods.
 */ 

// TODO: this is a very primitive, incorrect indenting method.
// It adds indents to all print statements except PrintChar()
// and does not keep track of whether the print cursor is at a new line,
// but can be helpful for debugging recursive function calls.

static uint32 printIndent = 0;

void SetPrintIndent(uint32 nChars)
{
	printIndent = nChars;
}

uint32 GetPrintIndent(void)
{
	return printIndent;
}

static void indent(void)
{
	for(index32 i = 0; i < printIndent; i++)
		fputc(' ', stdout);
}


/**
 * Stub for PrintF as a stop-gap measure for debug printing.
 * TODO: replaced with our own printf() implementation
 * to get rid of dependence on the C standard library
 */

#define FORMAT_STRING_BUFFER_SIZE	1024

// static char formatStringBuffer[FORMAT_STRING_BUFFER_SIZE];

size32 PrintF(char const * formatString, ...)
{
	va_list args;
	va_start(args, formatString);
	// TODO: this does not seem to handle varargs propely?
	// size32 nCharsPrinted = FormatString(
	// 	formatStringBuffer, FORMAT_STRING_BUFFER_SIZE, formatString, args);
	// PrintCString(formatStringBuffer);
	indent();
	size32 nCharsPrinted = vprintf(formatString, args);
	va_end(args);
	return nCharsPrinted;
}

void PrintChar(char c)
{
	fputc(c, stdout);
}


// NOTE: named to avoid conflict with PrintString() from string.h
void PrintCharString(char const * string, size32 length)
{
	for(index32 i = 0; i < length; i++)
		PrintChar(string[i]);
}


void PrintCString(char const * string)
{
	indent();
	fputs(string, stdout);
}


/**
 * printf-style string formatting.
 * TODO: replace vsnprintf() with our own implementation
 * to get rid of dependence on the C standard library
 */
size32 FormatString(char * buffer, size32 bufferSize, char const * formatString,...)
{
	va_list args;
	va_start(args, formatString);
	int result = vsnprintf(buffer, bufferSize, formatString, args);
	if(result < 0) {
		// NOTE: cannot use ASSERT() here as it relies on this function
		PrintCString("Error in FormatString()\n");
		AbortProgram();
	}
	size32 nCharsRequired = result;
	ASSERT(bufferSize >= nCharsRequired + 1);
	va_end(args);
	return nCharsRequired;
}


void CStringAppend(const char * suffix, char * buffer, size32 bufferSize)
{
	size32 length = CStringLength(buffer);
	CStringCopyLimited(suffix, buffer + length, bufferSize - length);
}


void CStringPrepend(const char * prefix, char * buffer, size32 bufferSize)
{
	char copy[bufferSize];
	CStringCopyLimited(buffer, copy, bufferSize);
	size32 prefixLength = CStringLength(prefix);
	CStringCopyLimited(prefix, buffer, bufferSize);
	CStringCopyLimited(copy, buffer + prefixLength, bufferSize - prefixLength);
}


//------------------------ File IO ------------------------


uint32 maxPathLength = PATH_MAX;

FileHandle OpenFile(char const * fileName)
{
	FILE * file = fopen(fileName, "rb");
	ASSERT(file);
    return (FileHandle) file;
}

// NOTE: can we get the file size without opening the file?
size64 GetFileSize(FileHandle fileHandle)
{
	FILE * file = (FILE *) fileHandle;
	size64 currentPosition = ftell(file);
    // get file length
    fseek(file, 0, SEEK_END);
    size64 fileSize = ftell(file);
    // restore original position
    fseek(file, currentPosition, SEEK_SET);
    return fileSize;
}

void ReadFromFile(FileHandle fileHandle, void * buffer, size64 readSize)
{
	FILE * file = (FILE *) fileHandle;
    ASSERT(fread(buffer, readSize, 1, file) == 1);
}


void CloseFile(FileHandle fileHandle)
{
	FILE * file = (FILE *) fileHandle;
    fclose(file);
}



bool FileExists(char const * filePath)
{
	return (access(filePath, F_OK) == 0);
}


bool DeleteFile(char const * filePath)
{
	// NOTE: this unlinks the file from the given file name.
	// If this is the only name for the file (no other hard links exist)
	// then the actual file is deleted.
	return(unlink(filePath) == 0);
}


// access mode specifiers
// #define FILEMAPPING_WRITABLE	0x01		// read and write access
// #define FILEMAPPING_CREATE      0x02        // create new mapping file if not already exists


static bool createFile(char const * fileName, int * fileDescriptor)
{
	*fileDescriptor = open(
		fileName,
		O_RDWR | O_CREAT,
		S_IRUSR | S_IWUSR
	);
	if(*fileDescriptor == -1) {
		int errorNumber = errno;
		PrintF("creating file '%s' failed, errno = %d\n", fileName, errorNumber);
		return false;		
	}
	return true;
}

static bool openFile(char const * fileName, int * fileDescriptor)
{
	*fileDescriptor = open(
		fileName,
		O_RDWR
	);
	if(*fileDescriptor == -1) {
		int errorNumber = errno;
		PrintF("opening file '%s' failed, errno = %d\n", fileName, errorNumber);
		return false;		
	}
	return true;
}

static size_t getFileSize(int fileDescriptor)
{
    struct stat fileStatus;
    ASSERT(fstat(fileDescriptor, &fileStatus) == 0);
    return fileStatus.st_size;
}

static void resizeFile(int fileDescriptor, size_t size)
{
	int resultCode = posix_fallocate(fileDescriptor, 0, size);
	ASSERT(resultCode == 0);
}


void GetParentDirectory(char * path, size32 bufferSize)
{
	// dirname() may return pointer to a read-only string at an undefined location,
	// or may return a pointer to buffer
	char const * parentPath = dirname(path);

	if(parentPath == path) {
		// dirname() just modified the given path, all done
	}
	else {
		// dirname() returned a pointer to another location
		ASSERT((parentPath < path) && (parentPath >= path + bufferSize));
		CStringCopyLimited(parentPath, path, bufferSize);
	}
}


/**
 * Get the full path string of the current running executable
 */
void GetExecutablePath(char * buffer, size32 bufferSize)
{
	// readlink does not append a zero terminator to the string, so we must have space for one
	ssize_t pathLength = readlink("/proc/self/exe", buffer, bufferSize - 1);
	ASSERT(pathLength != -1);
	buffer[pathLength] = '\0';
}


//---------------------- memory mapping ---------------------------

static void mapFileToMemory(void * address, size_t size, int fileDescriptor)
{
	// NOTE: we are currently using a fixed memory address.
	// I HOPE this is safe on linux, but the mmap documentation is a bit murky
	// see https://man7.org/linux/man-pages/man2/mmap.2.html
	void * actual_address = mmap(
		address,
		size,
		PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_FIXED,
		fileDescriptor,
		0						// offset
	);
	if(actual_address == MAP_FAILED) {
		int errorNumber = errno;
		PrintF("mapping failed, errno = %d\n", errorNumber);
		ASSERT(false);
	}
	if(actual_address != address) {
		PrintF(
			"mapping failed, expected address %lx, got %lx\n", 
			(addr64) address, (addr64) actual_address
		);
		ASSERT(false);
	}
}


bool RestoreMappedMemory(void * address, char const * fileName, FileMapping * mapping)
{
	int fileDescriptor;
	if(!openFile(fileName, &fileDescriptor))
		return false;

	mapping->size = getFileSize(fileDescriptor);
	mapping->address = address;

	mapFileToMemory(mapping->address, mapping->size, fileDescriptor);
	close(fileDescriptor);
	return true;
}


bool CreateMappedMemory(void * address, size_t size, char const * fileName, FileMapping * mapping)
{
	int fileDescriptor;
	if(!createFile(fileName, &fileDescriptor)) {
		mapping->size = 0;
		mapping->address = 0;
		return false;
	}
	
	resizeFile(fileDescriptor, size);
	mapping->size = size;
	mapping->address = address;

	mapFileToMemory(mapping->address, mapping->size, fileDescriptor);
	close(fileDescriptor);
	return true;
}


bool CreateOrRestoreMappedMemory(void * address, size_t size, char const * filePath, FileMapping * mapping)
{
	if(FileExists(filePath))
		return RestoreMappedMemory(address, filePath, mapping);
	else
		return CreateMappedMemory(address, size, filePath, mapping);
}


void ReleaseFileMapping(FileMapping * mapping)
{
	munmap(mapping->address, mapping->size);
}


void AbortProgram(void)
{
	abort();
}


uint64 RandomInteger(uint64 lowerBound, uint64 upperBound)
{
    // note: this distribution is somewhat skewed towards the lower bound
    uint64 intervalLength = upperBound - lowerBound + 1;
	return lowerBound + (rand() % intervalLength);
}


void SetRandomSeed(void)
{
	srand((uint32) time(NULL));
}


char const * GetEnvironmentVariable(char const * variableName)
{
	return getenv(variableName);
}