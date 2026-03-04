/**
 * Some windows-specific code to be used for a Windows platform layer in the future.
 */


#include <IntSafe.h>
#include <winbase.h>


struct s_FileMapInternals {
	HANDLE fileHandle;
	HANDLE mappingHandle;
};


static HANDLE openFile(char const * fileName, uint32 fileAccess)
{
	return CreateFile(
		fileName,
		fileAccess,
		0,						// request exclusive access to file
		NULL,					// lpSecurityAttributes, ignore
		OPEN_EXISTING,			// file must already exist
		FILE_ATTRIBUTE_NORMAL,	// dwFlagsAndAttributes
		NULL					// hTemplateFile
	);
}


static bool mapFileToMemory(char const * fileName, uint32 accessMode, FileMapping* mapping)
{
	// allocate internals, zeroed
	mapping->internals = calloc(1, sizeof(FileMapInternals));

	// TODO: could provide hints on data usage pattern here to optimize
	uint32 fileAccess = GENERIC_READ | ((accessMode & FILEMAPPING_WRITABLE) ? GENERIC_WRITE : 0);
	HANDLE fileHandle = openFile(fileName, fileAccess)
	if(fileHandle == INVALID_HANDLE_VALUE) {
		printf("MapFileToMemory(): failed to open file\n");
		ReleaseFileMapping(mapping);
		return false;
	}
	// TODO: 
	// get file size ( = size of mapping), 64-bit value
	GetFileSizeEx(fileHandle, (PLARGE_INTEGER) &(mapping->size));
	// create file mapping object
	uint32 protection = (accessMode & FILEMAPPING_WRITABLE) ? PAGE_READWRITE : PAGE_READONLY;
	HANDLE mappingHandle = CreateFileMappingA(
		fileHandle,
		NULL,					// lpFileMappingAttributes (use default)
		protection,				// flProtect, see above
		0, 0,					// mapping size equal to file size
		NULL					// name for the object (unnamed)
	);
	// map the file into memory
	uint32 mapAccess = (accessMode & FILEMAPPING_WRITABLE) ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ;
	mapping->address = MapViewOfFile(
		mappingHandle,
		mapAccess,				// dwDesiredAccess, see above
		0, 0,					// map from start of file
		0						// map entire file
	);
	if(mapping->address == NULL) {
		printf("MapFileToMemory(): failed to map file\n");
		ReleaseFileMapping(mapping);
		return false;
	}
	return true;
}


static void releaseFileMapping(FileMapping* mapping)
{
	if(mapping->address != 0)
		UnmapViewOfFile(mapping->address);
	if(mapping->internals->mappingHandle != 0)
		CloseHandle(mapping->internals->mappingHandle);
	if(mapping->internals->fileHandle != INVALID_HANDLE_VALUE)
		CloseHandle(mapping->internals->fileHandle);
}
