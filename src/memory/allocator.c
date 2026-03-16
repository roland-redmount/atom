/**
 * A buddy block memory allocator.
 * We currently do not split blocks, so we may over-allocate up to 2x
 */

#include "memory/allocator.h"
#include "platform.h"

// The virtual memory area to be used for allocation
// (We may have several of these in the future)
byte * memoryArea = 0;

// total free memory in bytes
size32 totalFree;

// the memory area size N and smallest block size B are always powers of 2
// this gives at M = N / B possible blocks of the smallest size, or
// log2 M = log2 N - log2 B.
//
// The allocator can be viewed as a complete binary tree with levels 
//    k = 0, 1, ... , log2 M
// where level 0 is the root node (entire area, block size N) and
// level log2 M are the leaves (size B). In general, size of a block
// at level k is size(k) = N / 2^k  <--> log2 size = log N - k
//

#define MIN_LOG_BLOCK_SIZE		6
#define MIN_BLOCK_SIZE			(1 << MIN_LOG_BLOCK_SIZE)


// We work with offsets into the memory area rather than pointers, since
//  (i) offsets will always be divisible
//  (ii) relocation (if needed) is easier.

typedef uint32 BlockOffset;

#define OFFSET_BITMASK (MIN_BLOCK_SIZE - 1)

// Zero is a valid offset (to the first block) so we need a different
// value for the "null pointer" offset.
// NOTE: since the block at offset 0 is always allocated,
// I think this the zero offset is actually never used?
#define NULL_OFFSET 0xFFFFFFFF


// Free block header structure, located at beginning of each free block

typedef struct FreeHeader {
	size8 logBlockSize;
	struct {
		uint8 free : 1;
		uint8 reserved : 7;
	} flags;
	size8 reserved[2];
	BlockOffset previous;
	BlockOffset next;
} FreeHeader;

#define FREE_HEADER_SIZE 12

// Allocated blocks store only the first 4 bytes of the above
// TODO: we could define a struct for this too

#define ALLOC_HEADER_SIZE 4


/**
 * The following data structure is always stored in the first block of
 * the memory area (to be persistent). For this reason one block is always
 * allocated, and the root node is never free; the largest available block is N / 2.
 *
 * offset   content
 * 0        FreeHeader
 * 12       size8 logAreaSize
 * 13-15    reserved
 * 16		BlockOffset freeLists[maxLevel + 1]
 *
 * For each level k, freeLists[k] is the head of a linked list of free blocks, implemented by
 * a header inserted into each free block.
 * 
 * TODO: this would be cleaner as a struct with a variable-size array freeLists[]
 */

#define ALLOCATOR_STRUCT_SIZE (FREE_HEADER_SIZE + 4)


static size8 logSizeToLevel(size8 logBlockSize, size8 logAreaSize)
{
	return logAreaSize - logBlockSize;
}

static size8 levelToLogSize(size8 blockLevel, size8 logAreaSize)
{
	return logAreaSize - blockLevel;
}

static size8 maxLevel(size8 logAreaSize)
{
	return logAreaSize - MIN_LOG_BLOCK_SIZE;
}

static size32 headerSize(size8 logAreaSize)
{
	return ALLOCATOR_STRUCT_SIZE + (maxLevel(logAreaSize) + 1)*sizeof(BlockOffset);
}

static size8 getLogAreaSize(void)
{
	return *((size8 *) (memoryArea + FREE_HEADER_SIZE));
}

static void setLogAreaSize(size8 logAreaSize)
{
	*((size8 *) (memoryArea + FREE_HEADER_SIZE)) = logAreaSize;
}

static BlockOffset * getFreeLists(void)
{
	return (BlockOffset *) (memoryArea + ALLOCATOR_STRUCT_SIZE);
}

// NOTE: could we simplify by expressing these as functions of logBlockSize,
// so we don't have to keep converting between level and logSize?

static BlockOffset getFreeList(uint8 level)
{
	return getFreeLists()[level];
}

// NOTE: level argument is strictly not needed here since it can be
//  computed from the block
static void setFreeList(uint8 level, BlockOffset block)
{
	getFreeLists()[level] = block;
}


static FreeHeader * getFreeHeader(BlockOffset offset)
{
	return (FreeHeader *) (memoryArea + offset);
}

static uint8 getLogBlockSize(BlockOffset block)
{
	return getFreeHeader(block)->logBlockSize;
}

static void setLogBlockSize(BlockOffset block, uint8 logBlockSize)
{
	getFreeHeader(block)->logBlockSize = logBlockSize;
}

static bool getFreeFlag(BlockOffset block)
{
	return getFreeHeader(block)->flags.free;
}

static void setFreeFlag(BlockOffset block, bool freeFlag)
{
	getFreeHeader(block)->flags.free = freeFlag;
}


static BlockOffset getNextFreeBlock(BlockOffset block)
{
	return getFreeHeader(block)->next;
}

static void setNextFreeBlock(BlockOffset block, BlockOffset next)
{
	getFreeHeader(block)->next = next;
}


static BlockOffset getPreviousFreeBlock(BlockOffset block)
{
	return getFreeHeader(block)->previous;
}

static void setPreviousFreeBlock(BlockOffset block, BlockOffset previous)
{
	getFreeHeader(block)->previous = previous;
}

static BlockOffset getBuddyOffset(BlockOffset block)
{
	// in the offset of a block of log size n, the bits 0, ... n-1 are zero
	// the buddy block offset is found by toggling bit n
	return block ^ (1 << getLogBlockSize(block));
}


/**
 * Calculate the minimal log block size required to fit the given allocation size
 */
static size8 requiredBlockLogSize(size32 allocSize)
{
	size32 memorySize = allocSize + ALLOC_HEADER_SIZE;
	uint8 logSize = GetHighestSetBit(memorySize - 1);
	return (logSize >= MIN_LOG_BLOCK_SIZE) ? logSize : MIN_LOG_BLOCK_SIZE;
}


void PrintFreeLists(void)
{
	PrintCString("freeLists:\n");
	BlockOffset * freeList = getFreeLists();
	for(size8 level = 0; level <= maxLevel(getLogAreaSize()); level++) {
		size32 size = 1 << levelToLogSize(level, getLogAreaSize());
		PrintF("level %u block size %x: ", level, size);
		BlockOffset block = freeList[level];
		while(block != NULL_OFFSET) {
			 PrintF(" 0x%x", block);
			 block = getNextFreeBlock(block);
			}
		PrintF(" (END)\n");
	}
	PrintChar('\n');
}

/**
 * TODO: function for dumping allocated memory.
 * We traverse the binary tree depth-first and
 * print any memory block not on the free list.
 */

static void dumpMemoryBlock(BlockOffset start)
{
	size32 size = 1 << getLogBlockSize(start);
	// We assume size is at least 64 bytes. We dump 16 bytes per row
	for(BlockOffset offset = start; offset < start + size; offset += 16) {
		byte const * memory = memoryArea + offset;
		// Dump memory in hex, 4 block of 4 bytes per row
		data32 const * data = (data32 *) memory;
		PrintF("%7x: %08x %08x %08x %08x   ", offset, data[0], data[1], data[2], data[3]);
		// Dump ASCII
		for(index32 i = 0; i < 16; i++)
			PrintChar(IsPrintableChar(memory[i]) ? memory[i] : '.');
		PrintChar('\n');
	}
}

static void dumpBlocksRecursive(size8 level, BlockOffset start, BlockOffset end, size8 maxLevel)
{
	size32 size = 1 << levelToLogSize(level, getLogAreaSize());
	BlockOffset freeBlock = getFreeList(level);
	for(BlockOffset offset = start; offset < end; offset += size) {
		if(freeBlock != NULL_OFFSET && offset == freeBlock) {
			// skip this one
			freeBlock = getNextFreeBlock(freeBlock); 
		}
		else {
			if(level == maxLevel) {
				// leaf, print memory
				dumpMemoryBlock(offset);
				PrintChar('\n');
			}
			else {
				// recurse down to next level
				dumpBlocksRecursive(level + 1, offset, offset + size, maxLevel);
			}
		}
	}
}

void DumpAllocatedBlocks(void)
{
	BlockOffset maxOffset = 1 << getLogAreaSize();
	PrintF("maxOffset = %x\n", maxOffset);
	dumpBlocksRecursive(0, 0, maxOffset, maxLevel(getLogAreaSize()));
}


/**
 * Add a block the free list for block's level.
 */
static void addToFreeList(BlockOffset block)
{
	size8 logSize = getLogBlockSize(block);
	size8 level = logSizeToLevel(logSize, getLogAreaSize());

	BlockOffset previousBlock = getFreeList(level);
	BlockOffset nextBlock;
	if(previousBlock == NULL_OFFSET) {
		// add the block as the first block on the list
		setFreeList(level, block);
		nextBlock = NULL_OFFSET;
	}
	else if(previousBlock > block) {
		setFreeList(level, block);
		nextBlock = previousBlock;
		previousBlock = NULL_OFFSET;
	}
	else {
		// Otherwise we have previousBlock < block.
		// Find insertion point in the free list to keep the list ordered.
		// NOTE: this is linear in number of free blocks ...
		while(true) {
			nextBlock = getNextFreeBlock(previousBlock);
			if(nextBlock == NULL_OFFSET || nextBlock > block)
				break;
			previousBlock = nextBlock;
		}
	}
	// set block pointers
	setPreviousFreeBlock(block, previousBlock);
	if(previousBlock != NULL_OFFSET)
		setNextFreeBlock(previousBlock, block);
	setNextFreeBlock(block, nextBlock);
	if(nextBlock != NULL_OFFSET)
		setPreviousFreeBlock(nextBlock, block);
}

/**
 * Remove a block to the beginning of the free list for block's level.
 * This maintains list ordering.
 */
static void removeFromFreeList(BlockOffset block)
{
	size8 logSize = getLogBlockSize(block);
	size8 level = logSizeToLevel(logSize, getLogAreaSize());

	BlockOffset next = getNextFreeBlock(block);
	BlockOffset previous = getPreviousFreeBlock(block);
	if(previous != NULL_OFFSET)
		setNextFreeBlock(previous, next);
	else
		setFreeList(level, next);
	if(next != NULL_OFFSET)
		setPreviousFreeBlock(next, previous);
}


/**
 * Recursively find a free block, splitting if needed.
 */
static BlockOffset allocateBlock(size8 logBlockSize)
{
	// start at the level matching the log block size
	size8 blockLevel = logSizeToLevel(logBlockSize, getLogAreaSize());
	BlockOffset block = getFreeList(blockLevel);
	if(block != NULL_OFFSET) {
		// remove first block from free list
		ASSERT(getFreeFlag(block) == true);
		removeFromFreeList(block);
		setFreeFlag(block, false);
		return block;
	}
	else {
		if(blockLevel == 0) 
			return NULL_OFFSET;		// not enough memory in allocator
		// try to allocate block from parent level
		block = allocateBlock(logBlockSize + 1);
		if(block == NULL_OFFSET)
			return NULL_OFFSET;
	
		// split parent block in two halves, allocate the first half
		setLogBlockSize(block, logBlockSize);
		BlockOffset buddy = getBuddyOffset(block);
		ASSERT(buddy > block)
		setLogBlockSize(buddy, logBlockSize);
		
		addToFreeList(buddy);
		setFreeFlag(buddy, true);
		return block;
	}
}


void CreateAllocator(void * address, size8 logAreaSize)
{
	ASSERT(sizeof(FreeHeader) == FREE_HEADER_SIZE);

	memoryArea = (byte *) address;
	setLogAreaSize(logAreaSize);

	size8 headerBlockLogSize = requiredBlockLogSize(
		headerSize(logAreaSize) - ALLOC_HEADER_SIZE);

	// add root block to level 0 free list
	BlockOffset rootBlock = 0;
	setLogBlockSize(rootBlock, logAreaSize);
	setPreviousFreeBlock(rootBlock, NULL_OFFSET);
	setNextFreeBlock(rootBlock, NULL_OFFSET);
	setFreeList(0, rootBlock);
	setFreeFlag(rootBlock, true);
	// all other free lists empty
	for(size8 i = 1; i <= maxLevel(logAreaSize); i++)
		setFreeList(i, NULL_OFFSET);

	// allocate the header block
	allocateBlock(headerBlockLogSize);
	totalFree = (1 << logAreaSize) - (1 << headerBlockLogSize);
}


void CloseAllocator(void)
{
	// nothing to do
	// caller manages deallocation of memory area
}

bool AllocatorIsEmpty(void)
{
	// check that allocator is in initial state
	BlockOffset* freeLists = getFreeLists();
	if(freeLists[0] != NULL_OFFSET)
		return false;

	size8 logAreaSize = getLogAreaSize();
	size8 headerBlockLogSize = requiredBlockLogSize(
		headerSize(logAreaSize) - ALLOC_HEADER_SIZE
	);
	size8 headerLevel = logSizeToLevel(headerBlockLogSize, logAreaSize);

	for(size8 i = 1; i <= headerLevel; i++) {
		BlockOffset block = freeLists[i];
		if(getLogBlockSize(block) != levelToLogSize(i, logAreaSize))
			return false;
		if(getPreviousFreeBlock(block) != NULL_OFFSET)
			return false;
		if(getNextFreeBlock(block) != NULL_OFFSET)
			return false;
		if(getFreeFlag(block) == false)
			return false;
	}

	for(size8 i = headerLevel + 1; i < maxLevel(logAreaSize); i++) {
		if(freeLists[0] != NULL_OFFSET)
			return false;
	}
	
	return true;
}


size32 GetTotalFree(void)
{
	return totalFree;
}

static void * offsetToAllocPointer(BlockOffset offset)
{
	return memoryArea + offset + ALLOC_HEADER_SIZE;
}

static BlockOffset allocPointerToOffset(void * allocPointer)
{
	byte * headerAddress = ((byte *) allocPointer) - ALLOC_HEADER_SIZE;
	ASSERT(headerAddress > memoryArea);

	BlockOffset offset = (BlockOffset) (headerAddress - memoryArea);
	ASSERT(offset < (1 << getLogAreaSize()));

	// verify block offset is valid
	ASSERT((offset & OFFSET_BITMASK) == 0);
	return offset;
}


void * Allocate(size32 allocSize)
{
	ASSERT(allocSize > 0);
	// printf("Allocate(size = %zu)\n", allocSize);
	size8 requiredLogSize = requiredBlockLogSize(allocSize);
	// PrintF("requiredLogSize = %u getLogAreaSize() = %u\n", requiredLogSize, getLogAreaSize());
	ASSERT(requiredLogSize < getLogAreaSize())

	BlockOffset block = allocateBlock(requiredLogSize);
	ASSERT(block != NULL_OFFSET);
	totalFree -= (1 << requiredLogSize);
	return offsetToAllocPointer(block);
}


// free block, recursively merging as needed
static void freeBlock(BlockOffset block)
{
	size8 logSize = getLogBlockSize(block);

	// check for a free buddy block
	BlockOffset buddy = getBuddyOffset(block);
	if((getLogBlockSize(buddy) == logSize) && getFreeFlag(buddy)) {
		// printf("merge buddy = 0x%x of 0x%x\n", buddy, block);
		removeFromFreeList(buddy);
		// always continue with leftmost block
		if(buddy < block) {
			block = buddy;
		}
		// merge blocks and recurse
		setLogBlockSize(block, logSize + 1);
		freeBlock(block);
	}
	else {
		addToFreeList(block);
		setFreeFlag(block, true);
	}
}


void Free(void * memory)
{
	BlockOffset block = allocPointerToOffset(memory);
	ASSERT(getFreeFlag(block) == false);
	// printf("Free 0x%x\n", block);
	size8 logSize = getLogBlockSize(block);
	freeBlock(block);
	totalFree += (1 << logSize);
}


size32 GetAllocatedSize(void * memory)
{
	BlockOffset block = allocPointerToOffset(memory);
	return (1 << getLogBlockSize(block)) - ALLOC_HEADER_SIZE;
}


void * Reallocate(void * memory, size32 newSize)
{
	if(!memory)
		return Allocate(newSize);
	
	BlockOffset block = allocPointerToOffset(memory);
	ASSERT(getFreeFlag(block) == false);
	
	size8 logSize = getLogBlockSize(block);
	size8 newLogSize = requiredBlockLogSize(newSize);
	if(newLogSize <= logSize) {
		// new allocation fits in same block
		return memory;
	}
	else {
		void * newMemory = Allocate(newSize);
		// copy the entire previous block into the first half
		// of the new block
		size32 bytesToCopy = (1 << logSize) - ALLOC_HEADER_SIZE;
		CopyMemory(memory, newMemory, bytesToCopy);
		Free(memory);
		return newMemory;
	}

}

