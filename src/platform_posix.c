/**
 * This is the platform layer for POSIX (Linux, MacOS) systems.
 * Most parts are the same across Linux and MacOS; differences
 * are handled with conditional compilation.
 */

// C standard library includes
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>		// for strtoll
#include <time.h>

// POSIX specific includes
#include <libgen.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/limits.h>
#elif defined(__APPLE__)
#include <limits.h>
#endif

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
 * Discard the rest of a line too long to fit the caller's buffer, so that the
 * next ReadLine() starts on the line after it.
 */
static void discardLine(void)
{
	int c;
	while(((c = fgetc(stdin)) != EOF) && (c != '\n'))
		;
}


int ReadLine(char * buffer, size32 bufferSize)
{
	ASSERT(bufferSize > 1)
	// a prompt ends in no line terminator, so it may still be buffered here
	fflush(stdout);
	if(!fgets(buffer, bufferSize, stdin))
		return READLINE_END;

	size32 length = CStringLength(buffer);
	if((length > 0) && (buffer[length - 1] == '\n')) {
		length--;
		// a line written on another platform may be terminated with \r\n
		if((length > 0) && (buffer[length - 1] == '\r'))
			length--;
		buffer[length] = 0;
		return READLINE_OK;
	}
	// No line terminator, so either the line was longer than the buffer, or the
	// input ended without one. A full buffer tells the two apart.
	if(length < bufferSize - 1)
		return READLINE_OK;
	discardLine();
	return READLINE_TOO_LONG;
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


#if PATH_MAX > MAX_PATH_LENGTH
#error "MAX_PATH_LENGTH is too small to be an upper bound on PATH_MAX"
#endif

uint32 maxPathLength = PATH_MAX;


FileHandle OpenFile(char const * fileName)
{
	// a file that is not there is a normal outcome, not a program error
	FILE * file = fopen(fileName, "rb");
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

bool ReadFromFile(FileHandle fileHandle, void * buffer, size64 readSize)
{
	FILE * file = (FILE *) fileHandle;
	return (fread(buffer, readSize, 1, file) == 1);
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
	if(fstat(fileDescriptor, &fileStatus) != 0)
		Panic("cannot determine file size, errno = %d\n", errno);
	return fileStatus.st_size;
}

static void resizeFile(int fileDescriptor, size_t size)
{
#ifdef __linux__
	int resultCode = posix_fallocate(fileDescriptor, 0, size);
	if(resultCode != 0)
		Panic("cannot allocate %zu bytes of file space, error %d\n", size, resultCode);
#elif defined(__APPLE__)
	// pre-allocate space for the file
	fstore_t store = {0};
	store.fst_flags = F_ALLOCATECONTIG;		// try for contiguous space first
	store.fst_posmode = F_PEOFPOSMODE;		// from end of file
	store.fst_offset = 0;
	store.fst_length = size;

	if(fcntl(fileDescriptor, F_PREALLOCATE, &store) == -1) {
		// If contiguous allocation fails, try non-contiguous
		store.fst_flags = F_ALLOCATEALL;
		if (fcntl(fileDescriptor, F_PREALLOCATE, &store) == -1) {
			close(fileDescriptor);
			Panic("cannot allocate %zu bytes of file space, errno = %d\n", size, errno);
		}
	}
	// Now actually set the file size
	if(ftruncate(fileDescriptor, size) == -1) {
		close(fileDescriptor);
		Panic("cannot set file size to %zu bytes, errno = %d\n", size, errno);
	}
#endif
}

// test if two strings overlap in memory
static bool nonOverlappingBuffers(char const * string1, size32 length1, char const * string2, size32 length2)
{
	return (string1 + length1 < string2) || (string2 + length2 < string1);
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
		size32 parentPathLength = CStringLength(parentPath);
		ASSERT(nonOverlappingBuffers(path, bufferSize, parentPath, parentPathLength));
		CStringCopyLimited(parentPath, path, bufferSize);
	}
}


/**
 * Get the full path string of the current running executable
 */
void GetExecutablePath(char * buffer, size32 bufferSize)
{
#ifdef __linux__
	// readlink does not append a zero terminator to the string, so we must have space for one
	ssize_t pathLength = readlink("/proc/self/exe", buffer, bufferSize - 1);
	if(pathLength == -1)
		Panic("cannot determine the executable path, errno = %d\n", errno);
	buffer[pathLength] = '\0';

#elif defined(__APPLE__)
	char pathBuffer[PATH_MAX];
	size32 pathBufferSize = PATH_MAX;
	if(_NSGetExecutablePath(pathBuffer, &pathBufferSize) != 0)
		Panic("cannot determine the executable path\n");
	// intermediate buffer to prevent overflow
	char realPathBuffer[PATH_MAX];
	// expand to absolute path
	if(realpath(pathBuffer, realPathBuffer) == 0)
		Panic("cannot resolve the executable path, errno = %d\n", errno);
	size32 realPathLength = CStringLength(realPathBuffer);
	ASSERT(realPathLength < bufferSize);
	CStringCopy(realPathBuffer, buffer);
#endif
}


void AppendPathComponent(char const * component, char * buffer, size32 bufferSize)
{
	size32 length = CStringLength(buffer);
	if(length > 0 && buffer[length - 1] != '/')
		CStringAppend("/", buffer, bufferSize);
	CStringAppend(component, buffer, bufferSize);
}


bool DirectoryExists(char const * path)
{
	// NOTE: FileExists() uses access(), which is also true for directories
	struct stat pathStatus;
	if(stat(path, &pathStatus) != 0)
		return false;
	return S_ISDIR(pathStatus.st_mode);
}


/**
 * Create a single directory, treating "already exists" as success.
 */
static bool createDirectory(char const * path)
{
	if(mkdir(path, S_IRWXU) == 0)
		return true;
	return (errno == EEXIST) && DirectoryExists(path);
}


bool EnsureDirectory(char const * path)
{
	// create each parent directory in turn by temporarily terminating the
	// path string at every separator
	char partialPath[maxPathLength + 1];
	CStringCopyLimited(path, partialPath, maxPathLength + 1);
	for(index32 i = 1; partialPath[i] != 0; i++) {
		if(partialPath[i] != '/')
			continue;
		partialPath[i] = 0;
		if(!createDirectory(partialPath))
			return false;
		partialPath[i] = '/';
	}
	return createDirectory(partialPath);
}


/**
 * Get a user directory as specified by the XDG base directory standard:
 * the given environment variable if set, otherwise the given path
 * relative to the user's home directory.
 *
 * NOTE: on MacOS the native location would be ~/Library/Application Support,
 * but we currently use the XDG locations on all POSIX platforms.
 */
static bool getUserDirectory(
	char const * variableName, char const * relativePath, char * buffer, size32 bufferSize)
{
	char const * directoryPath = GetEnvironmentVariable(variableName);
	if(directoryPath != 0 && directoryPath[0] != 0) {
		CStringCopyLimited(directoryPath, buffer, bufferSize);
		return true;
	}

	char const * homePath = GetEnvironmentVariable("HOME");
	if(homePath == 0 || homePath[0] == 0)
		return false;
	CStringCopyLimited(homePath, buffer, bufferSize);
	AppendPathComponent(relativePath, buffer, bufferSize);
	return true;
}


bool GetUserConfigDirectory(char * buffer, size32 bufferSize)
{
	return getUserDirectory("XDG_CONFIG_HOME", ".config", buffer, bufferSize);
}


bool GetUserDataDirectory(char * buffer, size32 bufferSize)
{
	return getUserDirectory("XDG_DATA_HOME", ".local/share", buffer, bufferSize);
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
	if(actual_address == MAP_FAILED)
		Panic("mapping failed, errno = %d\n", errno);
	if(actual_address != address)
		Panic(
			"mapping failed, expected address %lx, got %lx\n",
			(addr64) address, (addr64) actual_address
		);
}


bool RestoreMappedMemory(void * address, char const * fileName, FileMapping * fileMapping)
{
	int fileDescriptor;
	if(!openFile(fileName, &fileDescriptor)) {
		fileMapping->size = 0;
		fileMapping->address = 0;
		return false;
	}

	fileMapping->size = getFileSize(fileDescriptor);
	fileMapping->address = address;

	mapFileToMemory(fileMapping->address, fileMapping->size, fileDescriptor);
	close(fileDescriptor);
	return true;
}


bool CreateMappedMemory(void * address, size64 size, char const * fileName, FileMapping * fileMapping)
{
	int fileDescriptor;
	if(!createFile(fileName, &fileDescriptor)) {
		fileMapping->size = 0;
		fileMapping->address = 0;
		return false;
	}
	
	resizeFile(fileDescriptor, size);
	fileMapping->size = size;
	fileMapping->address = address;

	mapFileToMemory(fileMapping->address, fileMapping->size, fileDescriptor);
	close(fileDescriptor);
	return true;
}


bool CreateOrRestoreMappedMemory(void * address, size64 size, char const * filePath, FileMapping * fileMapping)
{
	if(FileExists(filePath))
		return RestoreMappedMemory(address, filePath, fileMapping);
	else
		return CreateMappedMemory(address, size, filePath, fileMapping);
}


void ReleaseFileMapping(FileMapping * fileMapping)
{
	if(fileMapping->address == 0)
		return;
	munmap(fileMapping->address, fileMapping->size);
}


void AbortProgram(void)
{
	// abort() does not flush, which would discard whatever we printed
	// to explain why we are aborting
	fflush(stdout);
	abort();
}


void Panic(char const * formatString, ...)
{
	va_list args;
	va_start(args, formatString);
	PrintCString("PANIC: ");
	vprintf(formatString, args);
	va_end(args);
	AbortProgram();
}


uint64 RandomInteger(uint64 lowerBound, uint64 upperBound)
{
	// note: this distribution is somewhat skewed towards the lower bound
	uint64 intervalLength = upperBound - lowerBound + 1;
	return lowerBound + (rand() % intervalLength);
}


void SetRandomSeed(uint32 seed)
{
	srand(seed);
}


uint32 GenerateRandomSeed(void)
{
	return (uint32) time(0);
}


char const * GetEnvironmentVariable(char const * variableName)
{
	return getenv(variableName);
}


bool IsPrintableChar(char c)
{
	return isprint(c);
}


bool IsDigitChar(char c)
{
	return isdigit(c);
}

bool IsSpaceChar(char c)
{
	return isspace(c);
}

bool IsAlpha(char c)
{
	return isalpha(c);
}


char ToLower(char c)
{
	ASSERT(IsAlpha(c));
	return tolower(c);
}

char ToUpper(char c)
{
	ASSERT(IsAlpha(c));
	return toupper(c);
}
