			
#include "memory/allocator.h"
#include "memory/pool.h"
#include "memory/paging.h"
#include "util/utilities.h"
#include "testing/testing.h"


// 2^12 = 4kb test area, yields 2^6 = 64 blocks
#define LOG_TEST_AREA_SIZE	12
#define TEST_AREA_SIZE		(1 << LOG_TEST_AREA_SIZE)

// max free size is half the total area minus the 4-byte header
#define MAX_BLOCK_SIZE		(TEST_AREA_SIZE >> 1)
#define MAX_ALLOC_SIZE		(MAX_BLOCK_SIZE - 4)

static void * memoryArea;
static size32 memoryAreaNPages;

static void setupMemoryAllocator(void)
{
	InitializePaging();
	memoryAreaNPages = PagesToFit(TEST_AREA_SIZE);
	memoryArea = AllocatePages(memoryAreaNPages);
	ASSERT(memoryArea);
	CreateAllocator(memoryArea, LOG_TEST_AREA_SIZE);
}


static void teardownMemoryAllocator(void)
{
	CloseAllocator();
	FreePages(memoryArea, memoryAreaNPages);
}


void testAllocate(void)
{
	size32 initialTotalFree = GetTotalFree();
	size32 totalFree = initialTotalFree;

	// since the allocated block header is 4 bytes,
	// 60 bytes is the maximum that fits inside a 64-byte block
	byte * memory = Allocate(60);
	ASSERT_UINT32_EQUAL(GetAllocatedSize(memory), 64 - 4)
	size32 newTotalFree = GetTotalFree();
	ASSERT_UINT32_EQUAL(newTotalFree, totalFree - 64)
	totalFree = newTotalFree;

	// a 61 byte allocation requires using a 128-byte block
	byte * memory2 = Allocate(61);
	ASSERT_UINT32_EQUAL(GetAllocatedSize(memory2), 128 - 4)
	newTotalFree = GetTotalFree();
	ASSERT_UINT32_EQUAL(newTotalFree, totalFree - 128)
	totalFree = newTotalFree;

	// allocate the maximum size block
	byte * memory3 = Allocate(MAX_ALLOC_SIZE);
	ASSERT_UINT32_EQUAL(GetAllocatedSize(memory3), MAX_BLOCK_SIZE - 4)
	newTotalFree = GetTotalFree();
	ASSERT_UINT32_EQUAL(newTotalFree, totalFree - MAX_BLOCK_SIZE)
	totalFree = newTotalFree;

	Free(memory);
	Free(memory2);
	Free(memory3);

	newTotalFree = GetTotalFree();
	ASSERT_UINT32_EQUAL(newTotalFree, initialTotalFree)
	ASSERT_TRUE(AllocatorIsEmpty())
}


void testAllocate2(void)
{
	// a particular case that has caused trouble before
	size32 sizes[7] = {678, 137, 659, 109, 477, 415, 141};
	void * memBlocks[7];
	for(index32 i = 0; i < 7; i++) {
		size32 size = sizes[i];
		memBlocks[i] = Allocate(size);
	}
	size32 totalFree = GetTotalFree();

	for(index32 i = 0; i < 7; i++) {
		if(memBlocks[i]) {
			uint32 blockSize = GetAllocatedSize(memBlocks[i]) + 4;
			Free(memBlocks[i]);
			size32 newTotalFree = GetTotalFree();
			ASSERT_UINT32_EQUAL(blockSize, newTotalFree - totalFree)
			totalFree = newTotalFree;
		}
	}
	ASSERT_TRUE(AllocatorIsEmpty());
}


#define N_RANDOM_BLOCKS	 10
#define N_FUZZ_ROUNDS	 100

void testFuzzAllocate(void)
{
	SetRandomSeed();
	size32 initialTotalFree = GetTotalFree();

	for(int k = 0; k < N_FUZZ_ROUNDS; k++) {
		void * memBlocks[N_RANDOM_BLOCKS];
		for(int i = 0; i < N_RANDOM_BLOCKS; i++) {
			size32 size = RandomInteger(1, MAX_ALLOC_SIZE / N_RANDOM_BLOCKS);
			memBlocks[i] = Allocate(size);
		}
		size32 totalFree = GetTotalFree();

		for(int i = 0; i < N_RANDOM_BLOCKS; i++) {
			if(memBlocks[i]) {
				uint32 blockSize = GetAllocatedSize(memBlocks[i]) + 4;
				Free(memBlocks[i]);
				size32 newTotalFree = GetTotalFree();
				ASSERT_UINT32_EQUAL(blockSize, newTotalFree - totalFree)
				totalFree = newTotalFree;
			}
		}
		ASSERT_UINT32_EQUAL(GetTotalFree(), initialTotalFree)
		ASSERT_TRUE(AllocatorIsEmpty())
	}
}


#define TEST_ALLOC_SIZE 		80
#define TEST_REALLOC_SIZE_1 	100
#define TEST_REALLOC_SIZE_2 	140

void testReallocate(void)
{
	// test data content
	byte bytes[TEST_ALLOC_SIZE] ;
	for(index32 i = 0; i < TEST_ALLOC_SIZE; i++)
		bytes[i] = i;

	// initial allocation using block size 128
	void * memory = Allocate(TEST_ALLOC_SIZE);
	CopyMemory(bytes, memory, TEST_ALLOC_SIZE);
	
	// reallocate while still fitting in same block
	void * reallocMemory = Reallocate(memory, TEST_REALLOC_SIZE_1);
	ASSERT_PTR_EQUAL(reallocMemory, memory)
	ASSERT_UINT32_EQUAL(CompareMemory(memory, reallocMemory, TEST_ALLOC_SIZE), 0)

	// reallocating to a larger block size
	reallocMemory = Reallocate(memory, TEST_REALLOC_SIZE_2);
	ASSERT_PTR_NOT_EQUAL(reallocMemory, memory)
	ASSERT_UINT32_EQUAL(CompareMemory(bytes, reallocMemory, TEST_ALLOC_SIZE), 0)

	Free(reallocMemory);

	// reallocating from null pointer is equivalent to Allocate()
	memory = Reallocate(0, TEST_ALLOC_SIZE);
	Free(memory);

	ASSERT_TRUE(AllocatorIsEmpty())
}


#define MAX_POOL_ITEMS			100
#define MAX_POOL_ITEM_SIZE		1000
#define N_POOL_FUZZ_ROUNDS		10
#define N_POOL_OPERATIONS		100

void testPoolAllocate(void)
{
	SetRandomSeed();
	
	for(int k = 0; k < N_POOL_FUZZ_ROUNDS; k++) {
		void * items[MAX_POOL_ITEMS];
		size32 nAllocated = 0;

		size32 itemSize = RandomInteger(1, MAX_POOL_ITEM_SIZE);		
		void * pool = CreatePool(itemSize);

		for(int i = 0; i < N_POOL_OPERATIONS; i++) {
			// randomly allocate or deallocate an item, within bounds
			bool allocate = (nAllocated == 0) ||
				((nAllocated < MAX_POOL_ITEMS) && (RandomInteger(0, 1) == 1));
			if(allocate)
				items[nAllocated++] = PoolAllocate(pool);
			else
				PoolFreeItem(pool, items[--nAllocated]);
			// PrintF("PoolNItems(pool) = %u, nAllocated = %u\n", PoolNItems(pool), nAllocated);
			ASSERT_TRUE(PoolNItems(pool) == nAllocated);
		}
		// deallocate remaining items
		while(nAllocated > 0)
			PoolFreeItem(pool, items[--nAllocated]);

		ASSERT_TRUE(PoolNItems(pool) == 0);
		FreePool(pool);

	}
}


int main(int argc, char * argv[])
{
	setupMemoryAllocator();

	testAllocate();
	testAllocate2();
	testFuzzAllocate();
	testReallocate();
	
	teardownMemoryAllocator();

	testPoolAllocate();

	TestSummary();
}

