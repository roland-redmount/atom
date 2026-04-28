/**
 * A buddy block memory allocator.
 * We currently do not split blocks, so we may over-allocate up to 2x
 */

#include "memory/allocator.h"
#include "platform.h"

#ifdef DEBUG_ALLOCATE
#include "btree/btree.h"

/**
 * Allocate() call records for debugging memory leaks.
 */


typedef struct {
	const char * fileName;
	uint32 lineNumber;
	void * address;
} AllocateRecord;

BTree * allocateLog;

int8 compareRecords(void const * item, void const * itemOrKey, size32 itemSize)
{
	AllocateRecord const * record1 = item;
	AllocateRecord const * record2 = itemOrKey;
	if(record1->address < record2->address)
		return -1;
	else if(record1->address > record2->address)
		return 1;
	else
		return 0;
}

#endif

struct {
	// The virtual memory area to be used for allocation
	// (We may have several of these in the future)
	byte * memoryArea;

	size32 initialNBytesFree;		// initial free memory, excluding header block
	size32 nBytesFree; 				// current total free memory
} allocator;


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
	return *((size8 *) (allocator.memoryArea + FREE_HEADER_SIZE));
}

static void setLogAreaSize(size8 logAreaSize)
{
	*((size8 *) (allocator.memoryArea + FREE_HEADER_SIZE)) = logAreaSize;
}

static BlockOffset * getFreeLists(void)
{
	return (BlockOffset *) (allocator.memoryArea + ALLOCATOR_STRUCT_SIZE);
}

// TODO: could we simplify by expressing these as functions of logBlockSize,
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
	return (FreeHeader *) (allocator.memoryArea + offset);
}

// NOTE: getFreeHeader(block) here is misleading since we
// use the first bytes of the "free header" also for blocks
// that are NOT free.
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
	size32 nBytesFree = 0;
	for(size8 level = 0; level <= maxLevel(getLogAreaSize()); level++) {
		size32 size = 1 << levelToLogSize(level, getLogAreaSize());
		PrintF("level %u block size 0x%x: ", level, size);
		BlockOffset block = freeList[level];
		while(block != NULL_OFFSET) {
			 PrintF(" 0x%x", block);
			 nBytesFree += size;
			 block = getNextFreeBlock(block);
			}
		PrintF(" (END)\n");
	}
	ASSERT(allocator.nBytesFree == nBytesFree)
	PrintF("Total %u bytes free, %u allocated.\n\n", nBytesFree, allocator.initialNBytesFree - nBytesFree);
}

/**
 * Dumping allocated memory.
 * We traverse the binary tree depth-first and
 * print any memory block not on the free list.
 */

static void dumpMemoryBlock(BlockOffset start, size32 size)
{
	// We assume size is at least 64 bytes. We dump 16 bytes per row
	for(BlockOffset offset = start; offset < start + size; offset += 16) {
		byte const * memory = allocator.memoryArea + offset;
		// Dump memory in hex, 4 block of 4 bytes per row, low bytes first
		PrintF("%7x: ", offset);
		for(index32 i = 0; i < 16; i += 4) {
			for(index32 j = 0; j < 4; j++)
				PrintF("%02x", memory[i + j]);
			PrintChar(' ');
		}
		PrintChar(' ');
		// Dump ASCII
		for(index32 i = 0; i < 16; i++)
			PrintChar(IsPrintableChar(memory[i]) ? memory[i] : '.');
		PrintChar('\n');
	}
}

/**
 * Recursively walk the tree at the given level, between start and end offsets,
 * and dump allocated memory blocks. The start and end offsets must be multiples
 * of the block size at the given level.
 * Returns the total number of bytes dumped.
 */
static size32 dumpBlocksRecursive(size8 level, BlockOffset start, BlockOffset end, size8 maxLevel)
{
	size32 size = 1 << levelToLogSize(level, getLogAreaSize());
	// find first free block >= start
	BlockOffset freeBlock = getFreeList(level);
	while(freeBlock != NULL_OFFSET && freeBlock < start)
		freeBlock = getNextFreeBlock(freeBlock);
	size32 nBytesAllocated = 0;
	for(BlockOffset offset = start; offset < end; offset += size) {
		if(freeBlock != NULL_OFFSET && offset == freeBlock) {
			// this block is free, skip to next
			freeBlock = getNextFreeBlock(freeBlock); 
		}
		else {
			if(level == maxLevel) {
				// allocated leaf
				PrintF("level = %u size 0x%x\n", level, size);
				size32 headerBlockSize = (1 << getLogBlockSize(0));
				if(offset < headerBlockSize)
					PrintF("%7x: Allocator header block\n\n", offset);
				else {
					dumpMemoryBlock(offset, size);
					PrintChar('\n');
					nBytesAllocated += size;
				}
			}
			else {
				// recurse down to next level
				nBytesAllocated += dumpBlocksRecursive(level + 1, offset, offset + size, maxLevel);
			}
		}
	}
	return nBytesAllocated;
}

void DumpAllocatedBlocks(void)
{
	// dump allocated memory starting after the header block
	BlockOffset maxOffset = 1 << getLogAreaSize();
	size32 nBytesAllocated = dumpBlocksRecursive(0, 0, maxOffset, maxLevel(getLogAreaSize()));
	ASSERT(nBytesAllocated == allocator.initialNBytesFree - allocator.nBytesFree);
	PrintF("Total %u bytes allocated.\n", nBytesAllocated);
}

#ifdef DEBUG_ALLOCATE
void DumpAllocateLog(void)
{
	BTreeIterator iterator;
	BTreeIterate(&iterator, allocateLog);
	while(BTreeIteratorHasItem(&iterator)) {
		AllocateRecord const * record = BTreeIteratorPeekItem(&iterator);
		PrintF("%x %s line %u\n",
			record->address, record->fileName, record->lineNumber);
		BTreeIteratorNext(&iterator);
	}
}
#endif

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
	}
	return block;
}


void CreateAllocator(void * address, size8 logAreaSize)
{
	ASSERT(sizeof(FreeHeader) == FREE_HEADER_SIZE);

	allocator.memoryArea = (byte *) address;
	setLogAreaSize(logAreaSize);

	// log size of the first "header" or block
	// storing the allocator free lists
	size8 headerBlockLogSize = requiredBlockLogSize(
		headerSize(logAreaSize) - ALLOC_HEADER_SIZE);

	// Setup free lists with a single free "root" block
	// in the level 0 free list covering the entire memory area.
	BlockOffset rootBlock = 0;
	setLogBlockSize(rootBlock, logAreaSize);
	setPreviousFreeBlock(rootBlock, NULL_OFFSET);
	setNextFreeBlock(rootBlock, NULL_OFFSET);
	setFreeList(0, rootBlock);
	setFreeFlag(rootBlock, true);
	// All other free lists are initially empty.
	for(size8 i = 1; i <= maxLevel(logAreaSize); i++)
		setFreeList(i, NULL_OFFSET);
	
	// Allocate the header block. This splits the level 0 "root" block
	// and then recursively splitting lower blocks, until we reach
	// the level that fits the header block size.
	allocateBlock(headerBlockLogSize);
	allocator.nBytesFree = (1 << logAreaSize) - (1 << headerBlockLogSize);
	allocator.initialNBytesFree = allocator.nBytesFree;
	
#ifdef DEBUG_ALLOCATE
	allocateLog = BTreeCreate(sizeof(AllocateRecord), &compareRecords, 0);
#endif
}


void CloseAllocator(void)
{
	// nothing to do
	// caller manages deallocation of memory area
#ifdef DEBUG_ALLOCATE
	// ensure all allocated items have been released
	ASSERT(BTreeNItems(allocateLog) == 0)
	BTreeFree(allocateLog);
#endif
}

bool AllocatorIsEmpty(void)
{
	// check that allocator is in initial state
	BlockOffset * freeLists = getFreeLists();
	// root block is always allocated
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


size32 AllocatorNBytesFree(void)
{
	return allocator.nBytesFree;
}


size32 AllocatorNBytesAllocated(void)
{
	return allocator.initialNBytesFree - allocator.nBytesFree;
}


size32 AllocatorMaxNBytes(void)
{
	return allocator.initialNBytesFree;
}


static void * offsetToAllocPointer(BlockOffset offset)
{
	return allocator.memoryArea + offset + ALLOC_HEADER_SIZE;
}


static BlockOffset allocPointerToOffset(void * allocPointer)
{
	byte * headerAddress = ((byte *) allocPointer) - ALLOC_HEADER_SIZE;
	ASSERT(headerAddress > allocator.memoryArea)

	BlockOffset offset = (BlockOffset) (headerAddress - allocator.memoryArea);
	ASSERT(offset < (1 << getLogAreaSize()))

	// verify block offset is valid
	ASSERT((offset & OFFSET_BITMASK) == 0)
	return offset;
}



#ifdef DEBUG_ALLOCATE
void * _Allocate(size32 allocSize)
#else
void * Allocate(size32 allocSize)
#endif
{
	ASSERT(allocSize > 0)
	size8 requiredLogSize = requiredBlockLogSize(allocSize);
	ASSERT(requiredLogSize < getLogAreaSize())

	BlockOffset block = allocateBlock(requiredLogSize);
	ASSERT(block != NULL_OFFSET)
	allocator.nBytesFree -= (1 << requiredLogSize);
	return offsetToAllocPointer(block);
}

#ifdef DEBUG_ALLOCATE
void * _LogAllocate(const char * fileName, uint32 lineNumber, size32 allocSize)
{
	void * block = _Allocate(allocSize);
	// cannot log allocation from btree.c since we use a B-tree for logging ..
	ASSERT(CStringCompare(fileName, "src/btree/btree.c") != 0)
	if(block) {
		AllocateRecord record = {
			.fileName = fileName, 
			.lineNumber = lineNumber,
			.address = block
		};
		BTreeInsert(allocateLog, &record);
	}
	return block;
}
#endif


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

#ifdef DEBUG_ALLOCATE
void _Free(void * memory)
#else
void Free(void * memory)
#endif
{
	BlockOffset block = allocPointerToOffset(memory);
	ASSERT(getFreeFlag(block) == false);
	// printf("Free 0x%x\n", block);
	size8 logSize = getLogBlockSize(block);
	freeBlock(block);
	allocator.nBytesFree += (1 << logSize);
}

#ifdef DEBUG_ALLOCATE
void _LogFree(const char * fileName, uint32 lineNumber, void * block)
{
	ASSERT(CStringCompare(fileName, "src/btree/btree.c") != 0)
	// Find record with this address
	AllocateRecord logRecord = {.address = block};
	if(!BTreeGetItem(allocateLog, &logRecord)) {
		// Attempted Free() of address not from Allocate()
		ASSERT(false);
	}
	_Free(block);
	BTreeDelete(allocateLog, &logRecord);
}
#endif

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

