
#include "platform.h"
#include "memory/paging.h"
#include "util/resources.h"
#include "util/utilities.h"

#define BITFIELD_SIZE_BYTES		(MEMORY_N_PAGES / 8)
#define	BITFIELD_N_PAGES		(BITFIELD_SIZE_BYTES / MEMORY_PAGE_SIZE + 1)

byte * pageTable = 0;

/**
 * The persistent root page, located on the page following the bit field pages.
 * It holds the paging state, and the state of kernel modules.
 */
#define ROOT_PAGE				BITFIELD_N_PAGES

// Magic number marking the root of a paging file; the bytes read "ATOMPAGE".
#define PAGING_FILE_MAGIC		0x41544F4D50414745


typedef struct s_PersistentRoot {
	data64 magic;
	uint32 formatVersion;
	size32 memorySize;
	// true while a process has the paging area in use; see ShutdownPaging()
	bool isOpen;
	index32 firstFreePage;
	void * states[N_STATE_KEYS];
} PersistentRoot;

static struct {
	MemoryDescriptor globalFileMap;
	PersistentRoot * root;
} paging;


static bool testPageBit(index32 page)
{
	index32 offset = page / 8;
	index8 bit = page & 7;
	byte bitMask = 1 << bit;
	return pageTable[offset] & bitMask;
}


static void setPageBit(index32 page)
{
	index32 offset = page / 8;
	index8 bit = page & 7;
	byte bitMask = 1 << bit;
	pageTable[offset] |= bitMask;
}


static void clearPageBit(index32 page)
{
	index32 offset = page / 8;
	index8 bit = page & 7;
	byte bitMask = ~(1 << bit);
	pageTable[offset] &= bitMask;
}


static index8 lowestClearedBit(byte x)
{
	index8 bit = 0;
	while(x & 1) {
		x = x >> 1;
		bit++;
	}
	return bit;
}


/**
 * Scan the bit field looking for the next free page,
 * starting from the given page. Returns the free page
 * number, or 0 if no free page could be found
 */
static index32 findFirstFreePage(index32 startPage)
{
	for(index32 offset = startPage / 8; offset < BITFIELD_SIZE_BYTES; offset++) {
		if(pageTable[offset] != 0xFF) {
			// this byte has at least one clear bit
			byte x = pageTable[offset];
			return offset * 8 + lowestClearedBit(x);
		}
	}
	return 0;  // page 0 can never be allocated anyway
}


/**
 * Find a range of nPages consecutive free pages, starting from startPage.
 * Returns the first free page of the range, or 0 if none found.
 * 
 * NOTE: this could probably be optimized quite a bit (no pun intended :)
 */
static index32 findFirstFreePages(index32 startPage, size32 nPages)
{
	size32 firstFreePage = 0;
	size32 nPagesFound = 0;
	
	for(index32 page = startPage; page < MEMORY_N_PAGES; page++) {
		if(!testPageBit(page)) {
			if(nPagesFound == 0)
				firstFreePage = page;
			nPagesFound++;
		}
		else {
			firstFreePage = 0;
			nPagesFound = 0;
		}
		if(nPagesFound == nPages)
			break;
	}
	return firstFreePage;
}


/**
 * The paging file lives in the data directory, which each test process
 * overrides so that tests do not share one paging file. See util/resources.h.
 */
static bool getPageFilePath(char * buffer, size32 bufferSize)
{
	return GetDataFilePath(PAGING_FILE_NAME, buffer, bufferSize);
}

// CLAUDE: Release the memory mapping, leaving the paging file as it is
static void releasePaging(void)
{
	ReleaseMemory(&(paging.globalFileMap));
	paging.globalFileMap = (MemoryDescriptor) {0};
	paging.root = 0;
	pageTable = 0;
}


/**
 * CLAUDE: Set up the page table and the persistent root of a new, blank paging area.
 */
static void formatPaging(void)
{
	// allocate bit field on first page(s)
	SetMemory(pageTable, BITFIELD_SIZE_BYTES, 0);
	for(index32 page = 0; page < BITFIELD_N_PAGES; page++)
		setPageBit(page);

	// Allocate the persistent root page.
	setPageBit(ROOT_PAGE);
	SetMemory(paging.root, sizeof(PersistentRoot), 0);
	paging.root->magic = PAGING_FILE_MAGIC;
	paging.root->formatVersion = PERSISTENCE_VERSION;
	paging.root->memorySize = MEMORY_SIZE;
	// The first free page follows the root page
	paging.root->firstFreePage = ROOT_PAGE + 1;
}


/**
 * Check that the the paging area is valid.
 * If not, returns false and prints the reason.
 * The pageFilePath is only used for printing.
 */
static bool validatePageArea(char const * pageFilePath)
{
	if(paging.globalFileMap.size != MEMORY_SIZE) {
		PrintF("The paging file %s has the wrong size.\n", pageFilePath);
		return false;
	}
	if(paging.root->magic != PAGING_FILE_MAGIC) {
		PrintF("The file %s is not a paging file.\n", pageFilePath);
		return false;
	}
	if((paging.root->formatVersion != PERSISTENCE_VERSION) ||
		(paging.root->memorySize != MEMORY_SIZE)) {
		PrintF("The paging file %s was written by a different version of atom.\n", pageFilePath);
		return false;
	}
	if(paging.root->isOpen) {
		PrintF("The paging file %s is in use, or was not closed properly.\n", pageFilePath);
		return false;
	}
	return true;
}


bool InitializePaging(uint32 memoryPersistence)
{
	ASSERT((memoryPersistence == TRANSIENT_MEMORY)
		| (memoryPersistence == NEW_PERSISTENT_MEMORY)
		| (memoryPersistence == RESTART_PERSISTENT_MEMORY));
	// verify we defined constants correctly
	ASSERT(MEMORY_SIZE == MEMORY_N_PAGES * MEMORY_PAGE_SIZE);
	// number of pages must be divisible by 8 for the bit field to use even number of bytes
	ASSERT((MEMORY_N_PAGES & 7) == 0);
	
	// create memory mapping
	char pageFilePath[maxPathLength + 1];
	if(memoryPersistence == TRANSIENT_MEMORY) {
		if(!CreateTransientMemory(MEMORY_SIZE, &(paging.globalFileMap)))
			Panic("InitializePaging() failed");
	}
	else {
		bool pathFound = getPageFilePath(pageFilePath, maxPathLength + 1);
		ASSERT(pathFound);
		if(memoryPersistence == NEW_PERSISTENT_MEMORY) {
			if(!CreateMappedMemory(
				FIXED_PAGING_ADDRESS, MEMORY_SIZE, pageFilePath, &(paging.globalFileMap)))
				Panic("InitializePaging() failed");
		}
		else {
			if(!FileExists(pageFilePath)) {
				PrintF("There is no paging file %s to restart from.\n", pageFilePath);
				return false;
			}
			if(!RestoreMappedMemory(FIXED_PAGING_ADDRESS, pageFilePath, &(paging.globalFileMap))) {
				PrintF("The paging file %s could not be restored.\n", pageFilePath);
				return false;
			}
		}
	}
	pageTable = paging.globalFileMap.address;
	paging.root = (PersistentRoot *) (pageTable + ROOT_PAGE * MEMORY_PAGE_SIZE);

	if(memoryPersistence == RESTART_PERSISTENT_MEMORY) {
		if(!validatePageArea(pageFilePath)) {
			// the root is left untouched, since the paging file may be in use by another process
			releasePaging();
			return false;
		}
	}
	else
		formatPaging();
	paging.root->isOpen = true;
	return true;
}


void ShutdownPaging(void)
{
	paging.root->isOpen = false;
	releasePaging();
}



void * GetPersistentState(PersistentStateKey key)
{
	ASSERT(key < N_STATE_KEYS)
	return paging.root->states[key];
}


void SetPersistentState(PersistentStateKey key, void * state)
{
	ASSERT(key < N_STATE_KEYS)
	paging.root->states[key] = state;
}


/**
 * Return a pointer to a page
 */
static void * pageToAddress(index32 page)
{
	return pageTable + page * MEMORY_PAGE_SIZE;
}

/**
 * Return the page index that the given pointer points into.
 * The pointer may point anywhere within the page.
 */
static index32 pointerToPage(void const * ptr)
{
	addr64 address = (addr64) ptr;
	addr64 baseAddress = (addr64) pageTable;
	// verify address is in range
	ASSERT((address > baseAddress) & (address < baseAddress + MEMORY_SIZE));
	// this division truncates to the page, as the arena starts on a page boundary
	return (address - baseAddress) / MEMORY_PAGE_SIZE;
}

/**
 * Return the page index corresponding to a page-aligned pointer
 */
static index32 pageAlignedPointerToPage(void const * ptr)
{
	// verify address is on an even page boundary
	ASSERT((((addr64) ptr) & (MEMORY_PAGE_SIZE - 1)) == 0);
	return pointerToPage(ptr);
}

bool IsPagedMemoryAddress(void const * address)
{
	addr64 baseAddress = (addr64) pageTable;
	return (pageTable != 0) &&
		((addr64) address >= baseAddress) && ((addr64) address < baseAddress + MEMORY_SIZE);
}


void * GetPageOfAddress(void const * address)
{
	return pageToAddress(pointerToPage(address));
}


void * AllocatePage(void)
{
	ASSERT(paging.root->firstFreePage);
	uint32 page = paging.root->firstFreePage;
	setPageBit(page);
	// clear page
	void * pageAddress = pageToAddress(page);
	SetMemory(pageAddress, MEMORY_PAGE_SIZE, 0);

	// find next free page
	paging.root->firstFreePage = findFirstFreePage(paging.root->firstFreePage);

	return pageAddress;
}


void FreePage(void const * pageAddress)
{
	index32 page = pageAlignedPointerToPage(pageAddress);
	clearPageBit(page);
	if(page < paging.root->firstFreePage)
		paging.root->firstFreePage = page;
}


void * AllocatePages(size32 nPages)
{
	ASSERT(nPages > 0);
	// find the first consecutive free pages
	ASSERT(paging.root->firstFreePage);
	index32 firstPage = findFirstFreePages(paging.root->firstFreePage, nPages);
	ASSERT(firstPage);
	// allocate pages
	for(index32 page = firstPage; page < firstPage + nPages; page++)
		setPageBit(page);
	if(paging.root->firstFreePage == firstPage)
		paging.root->firstFreePage = findFirstFreePage(firstPage + nPages);
	// clear pages
	void * firstPageAddress = pageToAddress(firstPage);
	SetMemory(firstPageAddress, MEMORY_PAGE_SIZE * nPages, 0);
	return firstPageAddress;
}


void FreePages(void const * firstPageAddress, size32 nPages)
{
	index32 firstPage = pageAlignedPointerToPage(firstPageAddress);
	for(index32 page = firstPage; page < firstPage + nPages; page++)
		clearPageBit(page);
	if(firstPage < paging.root->firstFreePage)
		paging.root->firstFreePage = firstPage;
}


uint32 PagesToFit(size32 nBytes)
{
	return DivCeiling(nBytes, MEMORY_PAGE_SIZE);
}

