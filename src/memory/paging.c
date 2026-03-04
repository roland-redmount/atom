
#include "platform.h"
#include "memory/paging.h"
#include "util/utilities.h"

#define BITFIELD_SIZE_BYTES		(MEMORY_N_PAGES / 8)
#define	BITFIELD_N_PAGES		(BITFIELD_SIZE_BYTES / MEMORY_PAGE_SIZE + 1)

byte * const pageTable = (byte *) BASE_ADDRESS;

static struct {
	FileMapping globalFileMap;
	uint32 firstFreePage;
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

static void getPageFilePath(char * buffer)
{
	/**
	 * TODO: we should figure out the appropriate place to store application data.
	 * On Linux it seems to be the XDG standard.
	 * We currently place the paging file in ~/.config/atom which is the typical
	 * folder for application data on Linux. We should create this folder when
	 * installing atom.
	 */
	const char * userHomePath = GetEnvironmentVariable("HOME");
	CStringCopyLimited(userHomePath, buffer, maxPathLength);
	CStringAppend("/.config/atom/", buffer, maxPathLength);
	CStringAppend(PAGING_FILE_NAME, buffer, maxPathLength);
}

void InitializePaging(void)
{
	// verify we are using a 64-bit compiler
	ASSERT(sizeof(void *) == 8);
	// verify we defined constants correctly
	ASSERT(MEMORY_SIZE == MEMORY_N_PAGES * MEMORY_PAGE_SIZE);
	// number of pages must be divisible by 8 for the bit field to use even number of bytes
	ASSERT((MEMORY_N_PAGES & 7) == 0);
	
	// create memory mapping
	char pageFilePath[maxPathLength + 1];
	getPageFilePath(pageFilePath);
	ASSERT(CreateOrRestoreMappedMemory(
		(void *) BASE_ADDRESS, MEMORY_SIZE, pageFilePath, &(paging.globalFileMap)));

	// allocate bit field on first page(s)
	SetMemory(pageTable, BITFIELD_SIZE_BYTES, 0);
	for(index32 page = 0; page < BITFIELD_N_PAGES; page++)
		setPageBit(page);
	// initial first free page follows bitfield pages
	paging.firstFreePage = BITFIELD_N_PAGES;
}


static void * pageToAddress(index32 page)
{
	return (void *) (BASE_ADDRESS + page * MEMORY_PAGE_SIZE);
}

static index32 pointerToPage(void * ptr)
{
	addr64 address = (addr64) ptr;
	// verify address is on an even page boundary
	ASSERT((address & (MEMORY_PAGE_SIZE - 1)) == 0);
	// verify address is in range
	ASSERT((address > BASE_ADDRESS) & (address < BASE_ADDRESS + MEMORY_SIZE));

	return (address - BASE_ADDRESS) / MEMORY_PAGE_SIZE;
}


void * AllocatePage(void)
{
	ASSERT(paging.firstFreePage);
	uint32 page = paging.firstFreePage;
	setPageBit(page);
	// clear page
	void * pageAddress = pageToAddress(page);
	SetMemory(pageAddress, MEMORY_PAGE_SIZE, 0);

	// find next free page
	paging.firstFreePage = findFirstFreePage(paging.firstFreePage);

	return pageAddress;
}


void FreePage(void * pageAddress)
{
	index32 page = pointerToPage(pageAddress);
	clearPageBit(page);
	if(page < paging.firstFreePage)
		paging.firstFreePage = page;
}


void * AllocatePages(size32 nPages)
{
	ASSERT(nPages > 0);
	// find the first consecutive free pages
	ASSERT(paging.firstFreePage);
	index32 firstPage = findFirstFreePages(paging.firstFreePage, nPages);
	ASSERT(firstPage);
	// allocate pages
	for(index32 page = firstPage; page < firstPage + nPages; page++)
		setPageBit(page);
	if(paging.firstFreePage == firstPage)
		paging.firstFreePage += nPages;
	// clear pages
	void * firstPageAddress = pageToAddress(firstPage);
	SetMemory(firstPageAddress, MEMORY_PAGE_SIZE * nPages, 0);
	return firstPageAddress;
}


void FreePages(void * firstPageAddress, size32 nPages)
{
	index32 firstPage = pointerToPage(firstPageAddress);
	for(index32 page = firstPage; page < firstPage + nPages; page++)
		clearPageBit(page);
	if(firstPage < paging.firstFreePage)
		paging.firstFreePage = firstPage;
}


uint32 PagesToFit(size32 nBytes)
{
	return DivCeiling(nBytes, MEMORY_PAGE_SIZE);
}

