
#include "platform.h"
#include "memory/paging.h"
#include "util/resources.h"
#include "testing/testing.h"

#include <unistd.h>
#include <sys/wait.h>


void testRestoreMapping(void)
{
	char mapFilePath[maxPathLength + 1];
	ASSERT_TRUE(GetFixtureFilePath("testmapping.txt", mapFilePath, maxPathLength + 1))
	MemoryDescriptor fileMapping;
	bool mappingSuccess = RestoreMappedMemory(FIXED_PAGING_ADDRESS, mapFilePath, &fileMapping);
	ASSERT_TRUE(mappingSuccess)
	// assertions only log, so we must return explicitly before using the mapping
	if(!mappingSuccess)
		return;
	ASSERT_UINT32_EQUAL(fileMapping.size, 10)

	char* fileContents = (char *) fileMapping.address;
	ASSERT_MEMORY_EQUAL(fileContents, "abcdefghij", 10)
	ReleaseMemory(&fileMapping);	
}


void testCreateMapping(void)
{
	char mapFilePath[maxPathLength + 1];
	ASSERT_TRUE(GetDataFilePath("tmp_mapping.txt", mapFilePath, maxPathLength + 1))
	uint32 memorySize = 1 << 20;	// 1 Mb
	MemoryDescriptor fileMapping;

	bool mappingSuccess = CreateMappedMemory(FIXED_PAGING_ADDRESS, memorySize, mapFilePath, &fileMapping);
	ASSERT_TRUE(mappingSuccess)
	if(!mappingSuccess)
		return;
	ASSERT_UINT32_EQUAL(fileMapping.size, memorySize)

	// write to mapped data
	char * memory = (char *) fileMapping.address;
	CopyMemory("abcdefghij", memory, 10);

	// release file, writing any changes to disk
	ReleaseMemory(&fileMapping);

	// TODO: check file contents is correct

	DeleteFile(mapFilePath);
}


/* CLAUDE: Pages, page contents and module state written in one session are
   there again after restarting from the paging file. */
void testRestartPaging(void)
{
	ASSERT_TRUE(InitializePaging(NEW_PERSISTENT_MEMORY))
	byte * page = AllocatePage();
	CopyMemory("abcdefghij", page, 10);
	SetModuleState(MODULE_KERNEL, page);
	ShutdownPaging();

	ASSERT_TRUE(InitializePaging(RESTART_PERSISTENT_MEMORY))
	ASSERT_PTR_EQUAL(GetModuleState(MODULE_KERNEL), page)
	ASSERT_MEMORY_EQUAL(page, "abcdefghij", 10)
	// the page table is restored, so the page is not handed out again
	byte * nextPage = AllocatePage();
	ASSERT_PTR_NOT_EQUAL(nextPage, page)
	FreePage(nextPage);
	FreePage(page);
	ShutdownPaging();
}


/* CLAUDE: A restart is refused when there is no paging file, when the file is
   not a paging file, and when the file was not closed by ShutdownPaging(). */
void testRestartRefused(void)
{
	char pageFilePath[maxPathLength + 1];
	ASSERT_TRUE(GetDataFilePath(PAGING_FILE_NAME, pageFilePath, maxPathLength + 1))

	DeleteFile(pageFilePath);
	ASSERT_FALSE(InitializePaging(RESTART_PERSISTENT_MEMORY))

	// a file of the wrong size
	MemoryDescriptor fileMapping;
	ASSERT_TRUE(CreateMappedMemory(0, MEMORY_PAGE_SIZE, pageFilePath, &fileMapping))
	ReleaseMemory(&fileMapping);
	ASSERT_FALSE(InitializePaging(RESTART_PERSISTENT_MEMORY))

	// a file of the right size, with no paging area in it
	ASSERT_TRUE(CreateMappedMemory(0, MEMORY_SIZE, pageFilePath, &fileMapping))
	ReleaseMemory(&fileMapping);
	ASSERT_FALSE(InitializePaging(RESTART_PERSISTENT_MEMORY))

	// a child process ends without closing its paging file, as after a crash
	pid_t child = fork();
	if(child == 0) {
		InitializePaging(NEW_PERSISTENT_MEMORY);
		_exit(0);
	}
	waitpid(child, 0, 0);
	ASSERT_FALSE(InitializePaging(RESTART_PERSISTENT_MEMORY))

	// a refused restart leaves the file open, so it is refused again
	ASSERT_FALSE(InitializePaging(RESTART_PERSISTENT_MEMORY))

	DeleteFile(pageFilePath);
}


int main(int argc, char * argv[])
{
	// CLAUDE: the tests delete the paging file, so they must not run in the user's data directory
	char const * dataDirectory = GetEnvironmentVariable("ATOM_DATA_DIR");
	if(!dataDirectory || !dataDirectory[0])
		Panic("test_persistence requires ATOM_DATA_DIR to be set; run it with ctest\n");

	testRestoreMapping();
	testCreateMapping();
	testRestartPaging();
	testRestartRefused();

	TestSummary();
}



