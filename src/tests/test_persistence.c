
#include "platform.h"
#include "memory/paging.h"
#include "util/resources.h"
#include "testing/testing.h"


void testRestoreMapping(void)
{
	char mapFilePath[maxPathLength + 1];
	ASSERT_TRUE(GetFixtureFilePath("testmapping.txt", mapFilePath, maxPathLength + 1))
	MemoryDescriptor fileMapping;
	bool mappingSuccess = RestoreMappedMemory(PAGING_ADDRESS_HINT, mapFilePath, &fileMapping);
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

	bool mappingSuccess = CreateMappedMemory(PAGING_ADDRESS_HINT, memorySize, mapFilePath, &fileMapping);
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


int main(int argc, char * argv[])
{
	testRestoreMapping();
	testCreateMapping();

	TestSummary();
}



