/**
 * Platform layer. Interfacing to the host system and C standard library goes here.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

// these libraries are (currently) always included
#include <stdbool.h>        // bool, true and false
#include <stdint.h>         // int8_t, uint8_t, int16_t etc


/**
 * Fixed-size numeric types. Correct sizes are enforced in kernel.c
 */
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef float float32;
typedef double float64;

/**
 * Types for arbitrary data that are not necessarily interpreted as numbers
 */
typedef uint8_t byte;
typedef uint8_t data8;
typedef uint16_t data16;
typedef uint32_t data32;
typedef uint64_t data64;

/**
 * Type names for used for index or offsets
 */
typedef uint8_t index8;
typedef uint16_t index16;
typedef uint32_t index32;

// maximum index value
#define INDEX_INFINITY UINT32_MAX


/**
 * Type names used for sizes of things, analogous to size_t
 */
typedef uint8_t size8;
typedef uint16_t size16;
typedef uint32_t size32;
typedef uint64_t size64;


/**
 * Unsigned integer used for pointer aritmetic, similar to uintptr_t
 */
typedef uint64_t addr64;


/**
 * Memory size unites
 * NOTE: on x64 (amd64) linux the user address space is 48 bits, up to 200 Tb
 */
#define KB   0x400L				// 1024
#define MB   (KB * KB)
#define GB   (MB * KB)
#define TB   (MB * MB)

/**
 * The ASSERT() macro is used to ensure conditions hold at various points
 * throughout the code base. It is defined only in DEBUG builds.
 *
 * NOTE: the condition is tested with an empty branch rather than negated,
 * so that it stays where the compiler can warn about an assignment used as
 * a condition. Writing if(!(condition)) puts the assignment in parentheses,
 * which tells the compiler we meant it, and hides typos like ASSERT(a = b).
 */
#ifdef DEBUG
#define ASSERT(condition) {\
	if(condition) {}\
	else {\
		PrintF("ASSERT() fail in %s(), %s:%d.\n", __func__, __FILE__, __LINE__);\
		AbortProgram();\
	}\
}
#else
#define ASSERT(condition) {if(condition) {}}
#endif


uint8 GetHighestSetBit(data32 x);


/**
 * Set all bytes in a memory region to a constant value.
 */
void SetMemory(void * address, size32 size, byte value);

/**
 * Copy the source memory block to the destination.
 * Source and destination may not overlap.
 */
void CopyMemory(void const * source, void * destination, size32 size);

/**
 * Copy the source memory block to the destination.
 * The 
 */
void MoveMemory(void const * source, void * destination, size32 size);	

/**
 * Compare two memory blocks lexiographically.
 */
int8 CompareMemory(void const * address1, void const * address2, size32 size);

size32 CStringLength(char const * string);
int8 CStringCompare(char const * string1, char const * string2);
int8 CStringCompareLimited(char const * string1, char const * string2, size32 maxLength);

/**
 * Copy a zero-terminated string source to destination, including the zero terminator.
 */
void CStringCopy(char const * source, char * destination);

/**
 * Copy a zero-terminated string source to destination, including the zero terminator.
 * Copies at most maxLength chars.
 */
void CStringCopyLimited(char const * source, char * destination, size32 maxLength);

char const * CStringFindChar(char const * string, char c);

/**
 * Append the suffix string to the end of the string stored in buffer.
 */
void CStringAppend(const char * suffix, char * buffer, size32 bufferSize);

/**
 * Prepend the suffix string to the string stored in buffer.
 */
void CStringPrepend(const char * prefix, char * buffer, size32 bufferSize);

int64 StringToInt64(char const * string, size32 length);
float64 StringToFloat64(char const * string, size32 length);

/**
 * Character classes
 */
bool IsPrintableChar(char c);

bool IsDigitChar(char c);

/**
 * Test if c is white space, such as a space, tab or newline
 */
bool IsSpaceChar(char c);

/**
 * Test if c is an alphabet letter
 */
bool IsAlpha(char c);


/**
 * Convert case for alphabet letters
 */
char ToLower(char c);
char ToUpper(char c);


/**
 * Printing to an output stream
 */

void PrintChar(char c);
void PrintCharString(char const * string, size32 length);
void PrintCString(char const * string);
size32 FormatString(char * buffer, size32 bufferSize, char const * formatString, ...);
size32 PrintF(char const * formatString, ...);

void SetPrintIndent(uint32 nChars);
uint32 GetPrintIndent(void);

/**
 * File I/O
 */

typedef data64 FileHandle;

FileHandle OpenFile(char const * filePath);
size64 GetFileSize(FileHandle fileHandle);
void ReadFromFile(FileHandle fileHandle, void * buffer, size64 readSize);
void CloseFile(FileHandle);

bool FileExists(char const * filePath);
bool DeleteFile(char const * filePath);

// path handling

/**
 * Upper bound on path lengths, for use where a compile time constant is
 * required (static buffers). The actual limit is maxPathLength, which the
 * platform layer guarantees is no larger than this.
 */
#define MAX_PATH_LENGTH 4096

extern uint32 maxPathLength;

void GetParentDirectory(char * path, size32 bufferSize);

/**
 * Get the full path string of the current running executable
 */
void GetExecutablePath(char * buffer, size32 bufferSize);

/**
 * Append a path component to the path in buffer, inserting a
 * path separator if needed.
 */
void AppendPathComponent(char const * component, char * buffer, size32 bufferSize);

/**
 * Test whether the given path exists and is a directory.
 */
bool DirectoryExists(char const * path);

/**
 * Create the directory at the given path, including any missing parent
 * directories. Succeeds if the directory already exists.
 */
bool EnsureDirectory(char const * path);

/**
 * Get the directory holding per-user configuration files, without trailing
 * separator. Returns false if no such directory can be determined, which
 * happens when the environment does not say where the user's home is.
 */
bool GetUserConfigDirectory(char * buffer, size32 bufferSize);

/**
 * Get the directory holding per-user application data, without trailing
 * separator. Returns false as GetUserConfigDirectory() does.
 */
bool GetUserDataDirectory(char * buffer, size32 bufferSize);

/**
 * Virtual memory
 */

typedef struct s_FileMapping {
	void * address;
	size64 size;
} FileMapping;

/**
 * Map a file into memory at the given address.
 * A missing or unreadable file is a normal outcome, not a program error:
 * these return false, and in that case the file mapping is zeroed, so that
 * the caller can safely pass it to ReleaseFileMapping().
 */
bool RestoreMappedMemory(void * address, char const * filePath, FileMapping * fileMapping);
bool CreateMappedMemory(void * address, size64 size, char const * filePath, FileMapping * fileMapping);
bool CreateOrRestoreMappedMemory(void * address, size64 size, char const * filePath, FileMapping * fileMapping);

/**
 * Release a file mapping, writing any changes to disk.
 * Releasing a zeroed (failed) mapping does nothing.
 */
void ReleaseFileMapping(FileMapping * fileMapping);

/**
 * Other
 */

char const * GetEnvironmentVariable(char const * variableName);
void AbortProgram(void);

/**
 * Generate a pseudorandom integer in the internal [lowerBound, upperBound], inclusive.
 * Upperbound must be no larger than RAND_MAX.
 * 
 * NOTE: this currently relies on rand() which may not be a good random generator.
 * Exactly which method is used depends on the host system.
 */
uint64 RandomInteger(uint64 lowerBound, uint64 upperBound);

/**
 * Set random seed
 */
void SetRandomSeed(uint32 seed);

/**
 * Generate a unique (more or less) random seed based on
 * an external device, typically the system clock.
 */
uint32 GenerateRandomSeed(void);


#endif  // PLATFORM_H
